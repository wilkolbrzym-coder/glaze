// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include "glaze/bson/common.hpp"
#include "glaze/core/opts.hpp"
#include "glaze/util/for_each.hpp"

namespace glz
{
   namespace detail
   {
      template <class T, class It, class End>
      GLZ_ALWAYS_INLINE T read_le(is_context auto&& ctx, It& it, End& end) noexcept
      {
         if (size_t(std::distance(it, end)) < sizeof(T)) [[unlikely]] {
            ctx.error = error_code::unexpected_end;
            return {};
         }
         T value;
         std::memcpy(&value, &(*it), sizeof(T));
         it += sizeof(T);
         if constexpr (std::endian::native == std::endian::little) {
            return value;
         }
         else {
            return swap_bytes(value);
         }
      }

      template <class It, class End>
      GLZ_ALWAYS_INLINE std::string_view read_bson_cstring(is_context auto&& ctx, It& it, End& end) noexcept
      {
         auto start_it = it;
         while (it != end && *it != '\0') {
            ++it;
         }
         if (it == end) [[unlikely]] {
            ctx.error = error_code::unexpected_end;
            return {};
         }
         std::string_view sv(reinterpret_cast<const char*>(&(*start_it)), std::distance(start_it, it));
         ++it; // Skip null terminator
         return sv;
      }

      template <class It, class End>
      void skip_bson_value(uint8_t type, is_context auto&& ctx, It& it, End& end) noexcept;
   }

   template <class T>
      requires(num_t<T> || char_t<T>)
   struct from<BSON, T>
   {
      template <auto Opts, class It, class End>
      GLZ_ALWAYS_INLINE static void op(auto&& value, is_context auto&& ctx, It& it, End& end) noexcept
      {
         value = detail::read_le<std::decay_t<T>>(ctx, it, end);
      }

      template <auto Opts, class It, class End>
      GLZ_ALWAYS_INLINE static void op(uint8_t type, auto&& value, is_context auto&& ctx, It& it, End& end) noexcept;
   };

   template <bool_t T>
   struct from<BSON, T>
   {
      template <auto Opts, class It, class End>
      GLZ_ALWAYS_INLINE static void op(auto&& value, is_context auto&& ctx, It& it, End& end) noexcept
      {
         if (it == end) [[unlikely]] {
            ctx.error = error_code::unexpected_end;
            return;
         }
         value = (*it != 0x00);
         ++it;
      }

      template <auto Opts, class It, class End>
      GLZ_ALWAYS_INLINE static void op(uint8_t type, auto&& value, is_context auto&& ctx, It& it, End& end) noexcept;
   };

   template <string_like T>
   struct from<BSON, T>
   {
      template <auto Opts, class It, class End>
      GLZ_ALWAYS_INLINE static void op(auto&& value, is_context auto&& ctx, It& it, End& end) noexcept
      {
         const uint32_t size = detail::read_le<uint32_t>(ctx, it, end);
         if (bool(ctx.error)) return;
         if (size == 0) return;
         if (size_t(std::distance(it, end)) < size) [[unlikely]] {
            ctx.error = error_code::unexpected_end;
            return;
         }

         if (it[size - 1] != '\0') [[unlikely]] {
            ctx.error = error_code::syntax_error;
            return;
         }

         if constexpr (std::same_as<std::decay_t<T>, std::string_view>) {
            value = {reinterpret_cast<const char*>(&(*it)), size - 1};
            it += size;
         }
         else {
            value.resize(size - 1);
            std::memcpy(value.data(), &(*it), size - 1);
            it += size;
         }
      }

      template <auto Opts, class It, class End>
      GLZ_ALWAYS_INLINE static void op(uint8_t type, auto&& value, is_context auto&& ctx, It& it, End& end) noexcept;
   };

   template <is_named_enum T>
   struct from<BSON, T>
   {
      template <auto Opts, class It, class End>
      GLZ_ALWAYS_INLINE static void op(auto&& value, is_context auto&& ctx, It& it, End& end) noexcept;
      template <auto Opts, class It, class End>
      GLZ_ALWAYS_INLINE static void op(uint8_t type, auto&& value, is_context auto&& ctx, It& it, End& end) noexcept;
   };

   template <class T>
      requires std::is_enum_v<T> && (!is_named_enum<T>)
   struct from<BSON, T>
   {
      template <auto Opts, class It, class End>
      GLZ_ALWAYS_INLINE static void op(auto&& value, is_context auto&& ctx, It& it, End& end) noexcept;
      template <auto Opts, class It, class End>
      GLZ_ALWAYS_INLINE static void op(uint8_t type, auto&& value, is_context auto&& ctx, It& it, End& end) noexcept;
   };

   template <nullable_like T>
   struct from<BSON, T>
   {
      template <auto Opts, class It, class End>
      GLZ_ALWAYS_INLINE static void op(auto&& value, is_context auto&& ctx, It& it, End& end) noexcept;
      template <auto Opts, class It, class End>
      GLZ_ALWAYS_INLINE static void op(uint8_t type, auto&& value, is_context auto&& ctx, It& it, End& end) noexcept;
   };

   template <is_variant T>
   struct from<BSON, T>
   {
      template <auto Opts, class It, class End>
      static void op(auto&& value, is_context auto&& ctx, It& it, End& end) noexcept;
      template <auto Opts, class It, class End>
      GLZ_ALWAYS_INLINE static void op(uint8_t type, auto&& value, is_context auto&& ctx, It& it, End& end) noexcept;
   };

   template <class T>
      requires glaze_object_t<T> || reflectable<T> || writable_map_t<T>
   struct from<BSON, T>
   {
      template <auto Opts, class It, class End>
      static void op(auto&& value, is_context auto&& ctx, It& it, End& end) noexcept;
      template <auto Opts, class It, class End>
      GLZ_ALWAYS_INLINE static void op(uint8_t type, auto&& value, is_context auto&& ctx, It& it, End& end) noexcept;
   };

   template <range T>
      requires(!string_like<T> && !writable_map_t<T>)
   struct from<BSON, T>
   {
      template <auto Opts, class It, class End>
      static void op(auto&& value, is_context auto&& ctx, It& it, End& end) noexcept;
      template <auto Opts, class It, class End>
      GLZ_ALWAYS_INLINE static void op(uint8_t type, auto&& value, is_context auto&& ctx, It& it, End& end) noexcept;
   };

   template <>
   struct parse<BSON>
   {
      template <auto Opts, class T, is_context Ctx, class It0, class It1>
      GLZ_ALWAYS_INLINE static void op(T&& value, Ctx&& ctx, It0&& it, It1&& end)
      {
         from<BSON, std::remove_cvref_t<T>>::template op<Opts>(std::forward<T>(value), std::forward<Ctx>(ctx),
                                                              std::forward<It0>(it), std::forward<It1>(end));
      }

      template <auto Opts, class T, is_context Ctx, class It0, class It1>
      GLZ_ALWAYS_INLINE static void op(uint8_t type, T&& value, Ctx&& ctx, It0&& it, It1&& end)
      {
         from<BSON, std::remove_cvref_t<T>>::template op<Opts>(type, std::forward<T>(value), std::forward<Ctx>(ctx),
                                                              std::forward<It0>(it), std::forward<It1>(end));
      }
   };
}

#include "glaze/core/read.hpp"

namespace glz
{
   namespace detail
   {
      template <class It, class End>
      void skip_bson_value(uint8_t type, is_context auto&& ctx, It& it, End& end) noexcept
      {
         switch (static_cast<bson_type>(type)) {
         case bson_type::Double:
            if (size_t(std::distance(it, end)) < 8) [[unlikely]] {
               ctx.error = error_code::unexpected_end;
               return;
            }
            it += 8;
            break;
         case bson_type::String:
         case bson_type::JavaScript:
         case bson_type::Symbol: {
            const uint32_t size = read_le<uint32_t>(ctx, it, end);
            if (bool(ctx.error)) return;
            if (size_t(std::distance(it, end)) < size) [[unlikely]] {
               ctx.error = error_code::unexpected_end;
               return;
            }
            it += size;
            break;
         }
         case bson_type::Document:
         case bson_type::Array: {
            const uint32_t size = read_le<uint32_t>(ctx, it, end);
            if (bool(ctx.error)) return;
            if (size < 4 || size_t(std::distance(it, end)) < size_t(size - 4)) [[unlikely]] {
               ctx.error = error_code::unexpected_end;
               return;
            }
            it += (size - 4);
            break;
         }
         case bson_type::Binary: {
            const uint32_t size = read_le<uint32_t>(ctx, it, end);
            if (bool(ctx.error)) return;
            if (size_t(std::distance(it, end)) < size_t(size + 1)) [[unlikely]] {
               ctx.error = error_code::unexpected_end;
               return;
            }
            it += (size + 1);
            break;
         }
         case bson_type::ObjectId:
            if (size_t(std::distance(it, end)) < 12) [[unlikely]] {
               ctx.error = error_code::unexpected_end;
               return;
            }
            it += 12;
            break;
         case bson_type::Boolean:
            if (it == end) [[unlikely]] {
               ctx.error = error_code::unexpected_end;
               return;
            }
            it += 1;
            break;
         case bson_type::DateTime:
            if (size_t(std::distance(it, end)) < 8) [[unlikely]] {
               ctx.error = error_code::unexpected_end;
               return;
            }
            it += 8;
            break;
         case bson_type::Null:
            break;
         case bson_type::Regex: {
            read_bson_cstring(ctx, it, end); // pattern
            if (bool(ctx.error)) return;
            read_bson_cstring(ctx, it, end); // options
            break;
         }
         case bson_type::Int32:
            if (size_t(std::distance(it, end)) < 4) [[unlikely]] {
               ctx.error = error_code::unexpected_end;
               return;
            }
            it += 4;
            break;
         case bson_type::Timestamp:
            if (size_t(std::distance(it, end)) < 8) [[unlikely]] {
               ctx.error = error_code::unexpected_end;
               return;
            }
            it += 8;
            break;
         case bson_type::Int64:
            if (size_t(std::distance(it, end)) < 8) [[unlikely]] {
               ctx.error = error_code::unexpected_end;
               return;
            }
            it += 8;
            break;
         case bson_type::Decimal128:
            if (size_t(std::distance(it, end)) < 16) [[unlikely]] {
               ctx.error = error_code::unexpected_end;
               return;
            }
            it += 16;
            break;
         default:
            break;
         }
      }
   }

   template <class T>
      requires(num_t<T> || char_t<T>)
   template <auto Opts, class It, class End>
   void from<BSON, T>::op(uint8_t type, auto&& value, is_context auto&& ctx, It& it, End& end) noexcept
   {
      using V = std::decay_t<T>;
      switch (static_cast<bson_type>(type)) {
      case bson_type::Double:
         value = static_cast<V>(detail::read_le<double>(ctx, it, end));
         break;
      case bson_type::Int32:
         value = static_cast<V>(detail::read_le<int32_t>(ctx, it, end));
         break;
      case bson_type::Int64:
         value = static_cast<V>(detail::read_le<int64_t>(ctx, it, end));
         break;
      case bson_type::Boolean:
         if (it == end) [[unlikely]] {
            ctx.error = error_code::unexpected_end;
            return;
         }
         value = static_cast<V>(*it != 0x00);
         ++it;
         break;
      default:
         ctx.error = error_code::syntax_error;
         detail::skip_bson_value(type, ctx, it, end);
      }
   }

   template <bool_t T>
   template <auto Opts, class It, class End>
   void from<BSON, T>::op(uint8_t type, auto&& value, is_context auto&& ctx, It& it, End& end) noexcept
   {
      if (type == uint8_t(bson_type::Boolean)) {
         if (it == end) [[unlikely]] {
            ctx.error = error_code::unexpected_end;
            return;
         }
         value = (*it != 0x00);
         ++it;
      }
      else {
         // Handle other types as booleans (0 is false, everything else true)
         using V = int64_t;
         V val{};
         parse<BSON>::template op<Opts>(type, val, ctx, it, end);
         value = (val != 0);
      }
   }

   template <string_like T>
   template <auto Opts, class It, class End>
   void from<BSON, T>::op(uint8_t type, auto&& value, is_context auto&& ctx, It& it, End& end) noexcept
   {
      if (type == uint8_t(bson_type::String)) {
         op<Opts>(value, ctx, it, end);
      }
      else {
         ctx.error = error_code::syntax_error;
         detail::skip_bson_value(type, ctx, it, end);
      }
   }

   template <is_named_enum T>
   template <auto Opts, class It, class End>
   void from<BSON, T>::op(auto&& value, is_context auto&& ctx, It& it, End& end) noexcept
   {
      std::string_view str;
      parse<BSON>::template op<Opts>(str, ctx, it, end);
      if (bool(ctx.error)) return;
      static constexpr auto N = reflect<T>::size;
      bool found = false;
      for_each<N>([&]<size_t I>() {
         if (found) return;
         if (str == reflect<T>::keys[I]) {
            value = glz::get<I>(reflect<T>::values);
            found = true;
         }
      });
      if (!found) {
         ctx.error = error_code::syntax_error;
      }
   }

   template <is_named_enum T>
   template <auto Opts, class It, class End>
   void from<BSON, T>::op(uint8_t type, auto&& value, is_context auto&& ctx, It& it, End& end) noexcept
   {
      if (type == uint8_t(bson_type::String)) {
         op<Opts>(value, ctx, it, end);
      }
      else if (type == uint8_t(bson_type::Int32) || type == uint8_t(bson_type::Int64)) {
         using U = std::underlying_type_t<std::decay_t<T>>;
         U underlying{};
         parse<BSON>::template op<Opts>(type, underlying, ctx, it, end);
         value = static_cast<std::decay_t<T>>(underlying);
      }
      else {
         ctx.error = error_code::syntax_error;
         detail::skip_bson_value(type, ctx, it, end);
      }
   }

   template <class T>
      requires std::is_enum_v<T> && (!is_named_enum<T>)
   template <auto Opts, class It, class End>
   void from<BSON, T>::op(auto&& value, is_context auto&& ctx, It& it, End& end) noexcept
   {
      using U = std::underlying_type_t<std::decay_t<T>>;
      U underlying{};
      parse<BSON>::template op<Opts>(underlying, ctx, it, end);
      value = static_cast<std::decay_t<T>>(underlying);
   }

   template <class T>
      requires std::is_enum_v<T> && (!is_named_enum<T>)
   template <auto Opts, class It, class End>
   void from<BSON, T>::op(uint8_t type, auto&& value, is_context auto&& ctx, It& it, End& end) noexcept
   {
      using U = std::underlying_type_t<std::decay_t<T>>;
      U underlying{};
      parse<BSON>::template op<Opts>(type, underlying, ctx, it, end);
      value = static_cast<std::decay_t<T>>(underlying);
   }

   template <nullable_like T>
   template <auto Opts, class It, class End>
   void from<BSON, T>::op(auto&& value, is_context auto&& ctx, It& it, End& end) noexcept
   {
      if constexpr (requires { value.emplace(); }) {
         value.emplace();
      }
      else {
         value = typename std::decay_t<T>::value_type{};
      }
      parse<BSON>::template op<Opts>(*value, ctx, it, end);
   }

   template <nullable_like T>
   template <auto Opts, class It, class End>
   void from<BSON, T>::op(uint8_t type, auto&& value, is_context auto&& ctx, It& it, End& end) noexcept
   {
      if (type == uint8_t(bson_type::Null)) {
         value.reset();
         return;
      }
      if constexpr (requires { value.emplace(); }) {
         value.emplace();
      }
      else {
         value = typename std::decay_t<T>::value_type{};
      }
      parse<BSON>::template op<Opts>(type, *value, ctx, it, end);
   }

   template <is_variant T>
   template <auto Opts, class It, class End>
   void from<BSON, T>::op(auto&& value, is_context auto&& ctx, It& it, End& end) noexcept
   {
      const auto start_it = it;
      const uint32_t total_size = detail::read_le<uint32_t>(ctx, it, end);
      if (bool(ctx.error)) return;

      if (it < start_it + total_size - 1) {
         const uint8_t type = static_cast<uint8_t>(*it);
         ++it;
         const std::string_view key = detail::read_bson_cstring(ctx, it, end);
         if (bool(ctx.error)) return;

         const auto type_index = variant_id_to_index<T>::op(key.data(), key.data() + key.size(), key.size());
         if (type_index < std::variant_size_v<std::decay_t<T>>) {
            if (value.index() != type_index) {
               emplace_runtime_variant(value, type_index);
            }
            std::visit([&](auto&& v) { parse<BSON>::template op<Opts>(type, v, ctx, it, end); }, value);
         }
         else {
            detail::skip_bson_value(type, ctx, it, end);
         }
      }

      it = start_it + total_size;
   }

   template <is_variant T>
   template <auto Opts, class It, class End>
   void from<BSON, T>::op(uint8_t type, auto&& value, is_context auto&& ctx, It& it, End& end) noexcept
   {
      if (type == uint8_t(bson_type::Document)) {
         op<Opts>(value, ctx, it, end);
      }
      else {
         ctx.error = error_code::syntax_error;
         detail::skip_bson_value(type, ctx, it, end);
      }
   }

   template <class T>
      requires glaze_object_t<T> || reflectable<T> || writable_map_t<T>
   template <auto Opts, class It, class End>
   void from<BSON, T>::op(auto&& value, is_context auto&& ctx, It& it, End& end) noexcept
   {
      const auto start_it = it;
      const uint32_t total_size = detail::read_le<uint32_t>(ctx, it, end);
      if (bool(ctx.error)) return;

      while (it < start_it + total_size - 1) {
         if (it == end) [[unlikely]] {
            ctx.error = error_code::unexpected_end;
            return;
         }
         const uint8_t type = static_cast<uint8_t>(*it);
         ++it;
         const std::string_view key = detail::read_bson_cstring(ctx, it, end);
         if (bool(ctx.error)) return;

         if constexpr (glaze_object_t<T> || reflectable<T>) {
            static constexpr auto N = reflect<T>::size;
            bool found = false;
            for_each<N>([&]<size_t I>() {
               if (found) return;
               if (key == reflect<T>::keys[I]) {
                  if constexpr (glaze_object_t<T>) {
                     parse<BSON>::template op<Opts>(type, get_member(value, glz::get<I>(reflect<T>::values)), ctx, it,
                                                    end);
                  }
                  else {
                     parse<BSON>::template op<Opts>(type, get_member(value, glz::get<I>(to_tie(value))), ctx, it, end);
                  }
                  found = true;
               }
            });
            if (!found) {
               detail::skip_bson_value(type, ctx, it, end);
            }
         }
         else {
            if constexpr (requires { value.find(key); }) {
               auto it_map = value.find(key);
               if (it_map != value.end()) {
                  parse<BSON>::template op<Opts>(type, it_map->second, ctx, it, end);
               }
               else {
                  parse<BSON>::template op<Opts>(type, value[std::string(key)], ctx, it, end);
               }
            }
            else {
               parse<BSON>::template op<Opts>(type, value[std::string(key)], ctx, it, end);
            }
         }
         if (bool(ctx.error)) return;
      }

      if (it < start_it + total_size && *it == '\0') {
         ++it;
      }
      else {
         it = start_it + total_size;
      }
   }

   template <class T>
      requires glaze_object_t<T> || reflectable<T> || writable_map_t<T>
   template <auto Opts, class It, class End>
   void from<BSON, T>::op(uint8_t type, auto&& value, is_context auto&& ctx, It& it, End& end) noexcept
   {
      if (type == uint8_t(bson_type::Document)) {
         op<Opts>(value, ctx, it, end);
      }
      else {
         ctx.error = error_code::syntax_error;
         detail::skip_bson_value(type, ctx, it, end);
      }
   }

   template <range T>
      requires(!string_like<T> && !writable_map_t<T>)
   template <auto Opts, class It, class End>
   void from<BSON, T>::op(auto&& value, is_context auto&& ctx, It& it, End& end) noexcept
   {
      const auto start_it = it;
      const uint32_t total_size = detail::read_le<uint32_t>(ctx, it, end);
      if (bool(ctx.error)) return;

      if constexpr (resizable<std::decay_t<T>>) {
         value.clear();
      }

      size_t i = 0;
      while (it < start_it + total_size - 1) {
         if (it == end) [[unlikely]] {
            ctx.error = error_code::unexpected_end;
            return;
         }
         const uint8_t type = static_cast<uint8_t>(*it);
         ++it;
         detail::read_bson_cstring(ctx, it, end); // skip key
         if (bool(ctx.error)) return;

         if constexpr (emplace_backable<std::decay_t<T>>) {
            parse<BSON>::template op<Opts>(type, value.emplace_back(), ctx, it, end);
         }
         else if constexpr (resizable<std::decay_t<T>>) {
            value.resize(i + 1);
            parse<BSON>::template op<Opts>(type, value[i], ctx, it, end);
         }
         else {
            if (i < value.size()) {
               parse<BSON>::template op<Opts>(type, value[i], ctx, it, end);
            }
            else {
               detail::skip_bson_value(type, ctx, it, end);
            }
         }
         if (bool(ctx.error)) return;
         ++i;
      }

      it = start_it + total_size;
   }

   template <range T>
      requires(!string_like<T> && !writable_map_t<T>)
   template <auto Opts, class It, class End>
   void from<BSON, T>::op(uint8_t type, auto&& value, is_context auto&& ctx, It& it, End& end) noexcept
   {
      if (type == uint8_t(bson_type::Array)) {
         op<Opts>(value, ctx, it, end);
      }
      else {
         ctx.error = error_code::syntax_error;
         detail::skip_bson_value(type, ctx, it, end);
      }
   }

   template <class T, class Buffer>
   [[nodiscard]] inline error_ctx read_bson(T&& value, Buffer&& buffer) noexcept
   {
      return read<opts{.format = BSON}>(std::forward<T>(value), std::forward<Buffer>(buffer));
   }
}
