// Glaze Library
// For the license information refer to glaze.hpp
// glz:header path="glaze/core/tuple.hpp"
// glz:header project_imports=ignore
module;

#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include "glaze/util/attributes.hpp"
#include "glaze/util/inline.hpp"

// glz::tuple is an aggregate tuple built for compile-time throughput.
//
// Each element lives in its own `elem<I, T>` base class. Element access is therefore a
// static_cast to a base rather than an overload set or a recursive walk: `get` is a single
// always-inlined cast that emits no code at any optimization level, including debug builds.
//
// A tuple of N elements costs N + 3 class instantiations, and the per-element ones are shared
// by every tuple that repeats an (index, type) pair, which is common across a reflected
// codebase. Nothing here is recursive, so cost grows linearly rather than quadratically with
// the number of elements.

// glz:emit prelude
export module glaze.core.tuple;

import std;
import glaze.core.basic_types;

namespace glz
{
   export template <class... Ts>
   struct tuple;

   export template <class T>
   using unwrap_ref_decay_t = typename std::unwrap_ref_decay<T>::type;

   namespace tuple_detail
   {
      // A single element. The index makes the base unique so a tuple may repeat a type, and
      // GLZ_NO_UNIQUE_ADDRESS lets empty elements (lambdas, member accessors) occupy no space.
      export template <glz::size_t I, class T>
      struct elem
      {
         GLZ_NO_UNIQUE_ADDRESS T value;
      };

      // Storage names only its element bases. Spelling the index pack and the type pack
      // separately here would double the length of the base's template argument list, which the
      // compiler then re-substitutes at every mention of a tuple, so the indices are consumed by
      // make_storage and never appear in the resulting type.
      export template <class... Elems>
      struct storage : Elems...
      {};

      export template <class Indices, class... Ts>
      struct make_storage;

      export template <glz::size_t... Is, class... Ts>
      struct make_storage<std::index_sequence<Is...>, Ts...>
      {
         using type = storage<elem<Is, Ts>...>;
      };

      export template <class... Ts>
      using storage_t = typename make_storage<std::index_sequence_for<Ts...>, Ts...>::type;

// __type_pack_element indexes a pack without instantiating anything, so prefer it where the
// compiler provides it. Define GLZ_DISABLE_TYPE_PACK_ELEMENT to fall back to the portable
// path on a compiler whose builtin misbehaves.
#if !defined(GLZ_DISABLE_TYPE_PACK_ELEMENT)
#if defined(__has_builtin)
#if __has_builtin(__type_pack_element)
#define GLZ_HAS_TYPE_PACK_ELEMENT
#endif
#endif
#if !defined(GLZ_HAS_TYPE_PACK_ELEMENT) && defined(__GNUC__) && !defined(__clang__) && (__GNUC__ >= 14)
#define GLZ_HAS_TYPE_PACK_ELEMENT
#endif
#endif

#ifdef GLZ_HAS_TYPE_PACK_ELEMENT
      export template <glz::size_t I, class... Ts>
      using nth_t = __type_pack_element<I, Ts...>;
#else
      // Recovers the Ith type by deducing it from the matching element base. Declared and never
      // defined, because it is only ever called in an unevaluated context.
      export template <glz::size_t I, class T>
      T nth_probe(const elem<I, T>&);

      export template <glz::size_t I, class... Ts>
      using nth_t = decltype(nth_probe<I>(std::declval<const storage_t<Ts...>&>()));
#endif

#undef GLZ_HAS_TYPE_PACK_ELEMENT

      export template <class T>
      inline constexpr bool is_tuple = false;

      export template <class... Ts>
      inline constexpr bool is_tuple<tuple<Ts...>> = true;

      export template <class From, class To>
      using match_const_t = std::conditional_t<std::is_const_v<From>, const To, To>;

      // Mirrors the set of types glz::get accepts: glz::tuple, anything subscriptable by an
      // index (std::array), and empty types. The empty case exists so that glz::tuple<> and
      // other stateless aggregates continue to satisfy concepts written in terms of get.
      export template <class T>
      concept indexable = is_tuple<std::remove_cvref_t<T>> || std::is_empty_v<std::remove_cvref_t<T>> ||
                          requires(T&& t) { static_cast<T &&>(t)[glz::size_t{}]; };
   }

   // A single overload keeps every call site to one candidate and one deduction, which matters
   // because reflected serialization resolves get<I> thousands of times per translation unit.
   export template <glz::size_t I, tuple_detail::indexable Tup>
   GLZ_ALWAYS_INLINE constexpr decltype(auto) get(Tup&& tup) noexcept
   {
      using D = std::remove_cvref_t<Tup>;
      if constexpr (tuple_detail::is_tuple<D>) {
         static_assert(I < D::N, "glz::get index is out of range");
         using T = typename D::template type_at<I>;
         if constexpr (std::is_reference_v<Tup> || std::is_const_v<std::remove_reference_t<Tup>>) {
            // An lvalue, or a const tuple of either value category: element access carries the
            // constness of the tuple, except for elements that are themselves references.
            using leaf = tuple_detail::match_const_t<std::remove_reference_t<Tup>, tuple_detail::elem<I, T>>;
            return (static_cast<leaf&>(tup).value);
         }
         else {
            return static_cast<T&&>(static_cast<tuple_detail::elem<I, T>&>(tup).value);
         }
      }
      else {
         return (static_cast<Tup&&>(tup)[I]);
      }
   }

   export template <class... Ts>
   struct tuple : tuple_detail::storage_t<Ts...>
   {
      static constexpr auto glaze_reflect = false;
      static constexpr glz::size_t N = sizeof...(Ts);

      template <glz::size_t I>
      using type_at = tuple_detail::nth_t<I, Ts...>;

      // Assignment from any other tuple-like of the same length. Preserves the implicitly
      // declared copy and move assignment operators.
      template <class U>
         requires(!std::same_as<std::remove_cvref_t<U>, tuple>)
      constexpr tuple& operator=(U&& other)
      {
         [&]<glz::size_t... Is>(std::index_sequence<Is...>) {
            ((element<Is>() = get<Is>(static_cast<U&&>(other))), ...);
         }(std::index_sequence_for<Ts...>{});
         return *this;
      }

      template <class... Us>
         requires(sizeof...(Us) == sizeof...(Ts))
      constexpr tuple& assign(Us&&... values)
      {
         [&]<glz::size_t... Is>(std::index_sequence<Is...>) {
            ((element<Is>() = static_cast<Us&&>(values)), ...);
         }(std::index_sequence_for<Ts...>{});
         return *this;
      }

      constexpr bool operator==(const tuple& other) const
         requires(std::equality_comparable<Ts> && ...)
      {
         return [&]<glz::size_t... Is>(std::index_sequence<Is...>) {
            return ((element<Is>() == other.template element<Is>()) && ...);
         }(std::index_sequence_for<Ts...>{});
      }

      constexpr auto operator<=>(const tuple& other) const
         requires(std::three_way_comparable<std::remove_reference_t<Ts>> && ...)
      {
         using ordering = std::common_comparison_category_t<std::compare_three_way_result_t<Ts>...>;
         ordering result = ordering::equivalent;
         [&]<glz::size_t... Is>(std::index_sequence<Is...>) {
            (void)(((result = element<Is>() <=> other.template element<Is>(), result != 0) || ...));
         }(std::index_sequence_for<Ts...>{});
         return result;
      }

      // Applies a function to every element in declaration order, so element 0 first, then
      // element 1, and so on, where element N is the one identified by get<N>.
      template <class F>
      constexpr void for_each(F&& func) &
      {
         [&]<glz::size_t... Is>(std::index_sequence<Is...>) {
            (void(func(element<Is>())), ...);
         }(std::index_sequence_for<Ts...>{});
      }
      template <class F>
      constexpr void for_each(F&& func) const&
      {
         [&]<glz::size_t... Is>(std::index_sequence<Is...>) {
            (void(func(element<Is>())), ...);
         }(std::index_sequence_for<Ts...>{});
      }
      template <class F>
      constexpr void for_each(F&& func) &&
      {
         [&]<glz::size_t... Is>(std::index_sequence<Is...>) {
            (void(func(static_cast<Ts&&>(element<Is>()))), ...);
         }(std::index_sequence_for<Ts...>{});
      }

      // Applies a function to each element in turn until one returns a truthy value. Returns
      // true if any application returned a truthy value, and false otherwise.
      template <class F>
      constexpr bool any(F&& func) &
      {
         return [&]<glz::size_t... Is>(std::index_sequence<Is...>) {
            return (bool(func(element<Is>())) || ...);
         }(std::index_sequence_for<Ts...>{});
      }
      template <class F>
      constexpr bool any(F&& func) const&
      {
         return [&]<glz::size_t... Is>(std::index_sequence<Is...>) {
            return (bool(func(element<Is>())) || ...);
         }(std::index_sequence_for<Ts...>{});
      }
      template <class F>
      constexpr bool any(F&& func) &&
      {
         return [&]<glz::size_t... Is>(std::index_sequence<Is...>) {
            return (bool(func(static_cast<Ts&&>(element<Is>()))) || ...);
         }(std::index_sequence_for<Ts...>{});
      }

      // Applies a function to each element in turn until one returns a falsy value. Returns
      // true if every application returned a truthy value, and false otherwise.
      template <class F>
      constexpr bool all(F&& func) &
      {
         return [&]<glz::size_t... Is>(std::index_sequence<Is...>) {
            return (bool(func(element<Is>())) && ...);
         }(std::index_sequence_for<Ts...>{});
      }
      template <class F>
      constexpr bool all(F&& func) const&
      {
         return [&]<glz::size_t... Is>(std::index_sequence<Is...>) {
            return (bool(func(element<Is>())) && ...);
         }(std::index_sequence_for<Ts...>{});
      }
      template <class F>
      constexpr bool all(F&& func) &&
      {
         return [&]<glz::size_t... Is>(std::index_sequence<Is...>) {
            return (bool(func(static_cast<Ts&&>(element<Is>()))) && ...);
         }(std::index_sequence_for<Ts...>{});
      }

     private:
      // Element access for the members above. Both the index and the type are known here, so
      // this skips the type lookup glz::get has to perform. Private member functions do not
      // affect the aggregate status the brace initialization of a tuple depends on.
      template <glz::size_t I>
      GLZ_ALWAYS_INLINE constexpr decltype(auto) element() & noexcept
      {
         return (static_cast<tuple_detail::elem<I, type_at<I>>&>(*this).value);
      }
      template <glz::size_t I>
      GLZ_ALWAYS_INLINE constexpr decltype(auto) element() const& noexcept
      {
         return (static_cast<const tuple_detail::elem<I, type_at<I>>&>(*this).value);
      }
   };

   export template <class... Ts>
   tuple(Ts...) -> tuple<unwrap_ref_decay_t<Ts>...>;

   export template <class... T>
   struct tuple_size;

   export template <class T>
   constexpr glz::size_t tuple_size_v = tuple_size<std::remove_const_t<T>>::value;

   export template <class... Ts>
   struct tuple_size<glz::tuple<Ts...>> : std::integral_constant<glz::size_t, sizeof...(Ts)>
   {};

   export template <class T, glz::size_t N>
   struct tuple_size<std::array<T, N>>
   {
      static constexpr glz::size_t value = N;
   };

   export template <class... Types>
   struct tuple_size<std::tuple<Types...>>
   {
      static constexpr glz::size_t value = sizeof...(Types);
   };

   export template <glz::size_t I, class... T>
   struct tuple_element;

   export template <glz::size_t I, class Tuple>
   using tuple_element_t = typename tuple_element<I, Tuple>::type;

   export template <glz::size_t I, class... Ts>
   struct tuple_element<I, glz::tuple<Ts...>>
   {
      using type = tuple_detail::nth_t<I, Ts...>;
   };

   export template <glz::size_t I, class... T>
   struct tuple_element<I, std::tuple<T...>>
   {
      using type = typename std::tuple_element<I, std::tuple<T...>>::type;
   };

   export template <glz::size_t I, class T1, class T2>
   struct tuple_element<I, std::pair<T1, T2>>
   {
      using type = std::conditional_t<I == 0, T1, T2>;
   };

   export template <class... Ts>
   constexpr tuple<Ts&...> tie(Ts&... ts) noexcept
   {
      return {{{ts}...}};
   }

   export template <class... Ts>
   constexpr auto make_tuple(Ts&&... args)
   {
      return tuple<unwrap_ref_decay_t<Ts>...>{{{static_cast<Ts&&>(args)}...}};
   }

   export template <class... Ts>
   constexpr auto forward_as_tuple(Ts&&... args) noexcept
   {
      return tuple<Ts&&...>{{{static_cast<Ts&&>(args)}...}};
   }

   export template <class F, class Tup>
      requires(tuple_detail::is_tuple<std::remove_cvref_t<Tup>>)
   constexpr decltype(auto) apply(F&& func, Tup&& tup)
   {
      return [&]<glz::size_t... Is>(std::index_sequence<Is...>) -> decltype(auto) {
         return static_cast<F&&>(func)(get<Is>(static_cast<Tup&&>(tup))...);
      }(std::make_index_sequence<tuple_size_v<std::remove_cvref_t<Tup>>>{});
   }

   namespace tuple_detail
   {
      // Maps each element of the concatenation back to the tuple it came from and its index
      // within that tuple, so tuple_cat is a single flat expansion rather than a fold of
      // pairwise concatenations.
      export template <glz::size_t Total>
      struct cat_layout
      {
         std::array<glz::size_t, Total> outer{};
         std::array<glz::size_t, Total> inner{};
      };

      export template <glz::size_t... Sizes>
      consteval auto make_cat_layout()
      {
         cat_layout<(Sizes + ... + 0)> layout{};
         constexpr std::array sizes{Sizes...};
         glz::size_t flat = 0;
         for (glz::size_t outer = 0; outer < sizes.size(); ++outer) {
            for (glz::size_t inner = 0; inner < sizes[outer]; ++inner) {
               layout.outer[flat] = outer;
               layout.inner[flat] = inner;
               ++flat;
            }
         }
         return layout;
      }

      // Layout is a template parameter rather than a local constexpr object so that its
      // members are usable as template arguments on every compiler.
      export template <auto Layout, class... Tups, glz::size_t... Is>
      constexpr auto cat_impl(tuple<Tups&&...>&& refs, std::index_sequence<Is...>)
      {
         return tuple<tuple_element_t<Layout.inner[Is], std::remove_cvref_t<nth_t<Layout.outer[Is], Tups...>>>...>{
            {{get<Layout.inner[Is]>(get<Layout.outer[Is]>(std::move(refs)))}...}};
      }
   }

   export template <class... Tups>
      requires(tuple_detail::is_tuple<std::remove_cvref_t<Tups>> && ...)
   constexpr auto tuple_cat(Tups&&... tups)
   {
      if constexpr (sizeof...(Tups) == 0) {
         return tuple<>{};
      }
      else {
         constexpr auto layout = tuple_detail::make_cat_layout<tuple_size_v<std::remove_cvref_t<Tups>>...>();

         // Every input is held as a reference so that each element is read exactly once, with
         // the value category of the tuple it came from.
         return tuple_detail::cat_impl<layout>(tuple<Tups&&...>{{{static_cast<Tups&&>(tups)}...}},
                                               std::make_index_sequence<layout.outer.size()>{});
      }
   }

   namespace tuplet
   {
      export using glz::forward_as_tuple;
      export using glz::make_tuple;
      export using glz::tuple_cat;

      // Converts a tuple into any type constructible from its elements.
      export template <class Tuple>
      struct convert final
      {
         Tuple tuple;

         template <class U>
         constexpr operator U() &&
         {
            return [&]<glz::size_t... Is>(std::index_sequence<Is...>) {
               return U{glz::get<Is>(static_cast<Tuple&&>(tuple))...};
            }(std::make_index_sequence<glz::tuple_size_v<std::remove_cvref_t<Tuple>>>{});
         }
      };

      export template <class Tuple>
      convert(Tuple&) -> convert<Tuple&>;
      export template <class Tuple>
      convert(const Tuple&) -> convert<const Tuple&>;
      export template <class Tuple>
      convert(Tuple&&) -> convert<Tuple>;
   }
}
