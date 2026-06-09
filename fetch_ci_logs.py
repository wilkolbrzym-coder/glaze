#!/usr/bin/env python3
"""
GitHub Actions Job Log Fetcher
-------------------------------
Fetches and searches for error/failure output in GitHub Actions job logs.

Usage:
    python3 fetch_ci_logs.py --token ghp_xxx --repo owner/repo --run-id 12345678
    python3 fetch_ci_logs.py --token ghp_xxx --repo owner/repo --job-ids 111 222 333

Requirements: Python 3.8+ (no third-party dependencies)
"""

import argparse
import ssl
import sys
import urllib.error
import urllib.request


# ---------------------------------------------------------------------------
# HTTP helpers
# ---------------------------------------------------------------------------

def _make_context() -> ssl.SSLContext:
    """Return an SSL context (verification disabled for corporate proxies)."""
    return ssl._create_unverified_context()


class _NoRedirectHandler(urllib.request.HTTPRedirectHandler):
    """Stop at the first redirect so we can strip the Authorization header
    before following the redirect to S3 (GitHub's log storage backend)."""

    def redirect_request(self, req, fp, code, msg, hdrs, newurl):
        raise urllib.error.HTTPError(req.full_url, code, msg, hdrs, fp)


def _github_get(url: str, token: str) -> bytes:
    """
    Perform a GitHub API GET request, following one redirect (to S3) without
    sending the Authorization header to the redirect target.
    """
    opener = urllib.request.build_opener(_NoRedirectHandler)
    req = urllib.request.Request(url)
    req.add_header("Authorization", f"token {token}")
    req.add_header("User-Agent", "glaze-ci-log-fetcher/1.0")
    req.add_header("Accept", "application/vnd.github+json")

    try:
        with opener.open(req) as resp:
            return resp.read()
    except urllib.error.HTTPError as exc:
        if exc.code in (301, 302, 303, 307, 308):
            redirect_url = exc.headers.get("Location", "")
            req2 = urllib.request.Request(redirect_url)
            req2.add_header("User-Agent", "glaze-ci-log-fetcher/1.0")
            with urllib.request.urlopen(req2, context=_make_context()) as resp:
                return resp.read()
        raise


# ---------------------------------------------------------------------------
# GitHub API helpers
# ---------------------------------------------------------------------------

def get_jobs_for_run(repo: str, run_id: int, token: str) -> list[dict]:
    """Return list of job dicts for a workflow run."""
    url = f"https://api.github.com/repos/{repo}/actions/runs/{run_id}/jobs"
    import json
    data = json.loads(_github_get(url, token))
    return data.get("jobs", [])


def fetch_job_log(repo: str, job_id: int, token: str) -> str:
    """Download the raw text log for a single job."""
    url = f"https://api.github.com/repos/{repo}/actions/jobs/{job_id}/logs"
    return _github_get(url, token).decode("utf-8", errors="ignore")


# ---------------------------------------------------------------------------
# Log analysis
# ---------------------------------------------------------------------------

ERROR_PATTERNS = [
    "error:",
    "fatal error",
    "undefined reference",
    "no matching function",
    "static assertion failed",
    "error c",        # MSVC  e.g.  error C2338
    "linker error",
]

CONTEXT_BEFORE = 4
CONTEXT_AFTER  = 6


def find_errors(log: str) -> list[str]:
    """Return annotated error-context snippets found in *log*."""
    lines = log.splitlines()
    snippets: list[str] = []
    for i, line in enumerate(lines):
        lower = line.lower()
        if any(pat in lower for pat in ERROR_PATTERNS):
            start = max(0, i - CONTEXT_BEFORE)
            end   = min(len(lines), i + CONTEXT_AFTER + 1)
            header = f"--- Error at line {i + 1} ---"
            snippets.append(header)
            snippets.extend(lines[start:end])
            snippets.append("")
    return snippets


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Fetch GitHub Actions logs and extract build errors."
    )
    p.add_argument("--token",   required=True,  help="GitHub Personal Access Token")
    p.add_argument("--repo",    required=True,  help="owner/repo  e.g. wilkolbrzym-coder/glaze")
    group = p.add_mutually_exclusive_group(required=True)
    group.add_argument("--run-id",  type=int,   help="Workflow run ID (fetches all jobs)")
    group.add_argument("--job-ids", type=int,   nargs="+", help="One or more job IDs")
    p.add_argument("--max-errors",  type=int,   default=20,
                   help="Maximum error snippets to print per job (default: 20)")
    p.add_argument("--all",         action="store_true",
                   help="Print full log instead of only error snippets")
    return p.parse_args()


def process_job(repo: str, job_id: int, job_name: str,
                token: str, max_errors: int, show_all: bool) -> None:
    print(f"\n{'=' * 70}")
    print(f"JOB: {job_name}  (id={job_id})")
    print(f"{'=' * 70}")
    try:
        log = fetch_job_log(repo, job_id, token)
    except Exception as exc:
        print(f"  [ERROR fetching log]: {exc}")
        return

    lines = log.splitlines()
    print(f"  Total log lines: {len(lines)}")

    if show_all:
        print(log)
        return

    snippets = find_errors(log)
    if not snippets:
        print("  No error patterns found. Printing last 30 lines:")
        print("\n".join(lines[-30:]))
    else:
        shown = 0
        for snippet in snippets:
            if shown >= max_errors:
                print(f"  ... (truncated after {max_errors} error snippets)")
                break
            print(snippet)
            if snippet.startswith("--- Error"):
                shown += 1


def main() -> None:
    args = parse_args()

    if args.run_id:
        print(f"Fetching jobs for run {args.run_id} in {args.repo} ...")
        try:
            jobs = get_jobs_for_run(args.repo, args.run_id, args.token)
        except Exception as exc:
            print(f"Failed to list jobs: {exc}", file=sys.stderr)
            sys.exit(1)

        if not jobs:
            print("No jobs found for this run.")
            sys.exit(0)

        # Only process failed jobs unless --all is specified at the run level
        failed = [j for j in jobs if j.get("conclusion") in ("failure", "cancelled", None)]
        target_jobs = failed if failed else jobs
        print(f"Found {len(jobs)} jobs, processing {len(target_jobs)} (failed/in-progress).")

        for job in target_jobs:
            process_job(
                repo      = args.repo,
                job_id    = job["id"],
                job_name  = job.get("name", str(job["id"])),
                token     = args.token,
                max_errors= args.max_errors,
                show_all  = args.all,
            )
    else:
        for job_id in args.job_ids:
            process_job(
                repo      = args.repo,
                job_id    = job_id,
                job_name  = str(job_id),
                token     = args.token,
                max_errors= args.max_errors,
                show_all  = args.all,
            )


if __name__ == "__main__":
    main()
