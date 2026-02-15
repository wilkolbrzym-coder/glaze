// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include "glaze/bson/common.hpp"
#include "glaze/core/opts.hpp"
#include "glaze/core/write.hpp"
#include "glaze/core/to.hpp"
#include "glaze/util/dump.hpp"
#include "glaze/util/for_each.hpp"
#include "glaze/util/itoa.hpp"

namespace glz
{
   namespace detail
   {
      template <class B>
      GLZ_ALWAYS_INLINE void write_bson_size(const uint32_t size, B& b, size_t ix) noexcept
      {
         if constexpr (std::endian::native == std::endian::little) {
            std::memcpy(&b[ix], &size, 4);
         }
         else {
            const uint32_t swapped = glz::detail::swap_bytes(size);
            std::memcpy(&b[ix], &swapped, 4);
         }
      }
   }

   template <>
   struct serialize<BSON>
   {
      template <auto Opts, class T, is_context Ctx, class B, class IX>
      GLZ_ALWAYS_INLINE static void op(T&& value, Ctx&& ctx, B&& b, IX&& ix)
      {
         to<BSON, std::remove_cvref_t<T>>::template op<Opts>(std::forward<T>(value), std::forward<Ctx>(ctx),
                                                                std::forward<B>(b), std::forward<IX>(ix));
      }
   };

   template <class T>
      requires(num_t<T> || char_t<T>)
   struct to<BSON, T>
   {
      template <auto Opts, class B, class IX>
      GLZ_ALWAYS_INLINE static void op(auto&& value, is_context auto&&, B& b, IX& ix) noexcept
      {
         using V = std::decay_t<T>;
         if constexpr (std::same_as<V, float>) {
             dump_le<double>(static_cast<double>(value), b, ix);
         } else {
             dump_le<V>(value, b, ix);
         }
      }
   };

   template <bool_t T>
   struct to<BSON, T>
   {
      template <auto Opts, class B, class IX>
      GLZ_ALWAYS_INLINE static void op(const bool value, is_context auto&&, B& b, IX& ix) noexcept
      {
         dump(static_cast<char>(value ? 0x01 : 0x00), b, ix);
      }
   };

   template <string_like T>
   struct to<BSON, T>
   {
      template <auto Opts, class B, class IX>
      GLZ_ALWAYS_INLINE static void op(auto&& value, is_context auto&&, B& b, IX& ix) noexcept
      {
         const uint32_t size = static_cast<uint32_t>(value.size()) + 1;
         dump_le<uint32_t>(size, b, ix);
         dump(value, b, ix);
         dump('\0', b, ix);
      }
   };

   template <is_named_enum T>
   struct to<BSON, T>
   {
      template <auto Opts, class B, class IX>
      GLZ_ALWAYS_INLINE static void op(auto&& value, is_context auto&& ctx, B& b, IX& ix) noexcept
      {
         const sv str = get_enum_name(value);
         to<BSON, sv>::template op<Opts>(str, ctx, b, ix);
      }
   };

   template <class T>
      requires std::is_enum_v<T> && (!is_named_enum<T>)
   struct to<BSON, T>
   {
      template <auto Opts, class B, class IX>
      GLZ_ALWAYS_INLINE static void op(auto&& value, is_context auto&& ctx, B& b, IX& ix) noexcept
      {
         using U = std::underlying_type_t<std::decay_t<T>>;
         to<BSON, U>::template op<Opts>(static_cast<U>(value), ctx, b, ix);
      }
   };

   template <nullable_like T>
   struct to<BSON, T>
   {
      template <auto Opts, class Value, is_context Ctx, class B, class IX>
      GLZ_ALWAYS_INLINE static void op(Value&& value, Ctx&& ctx, B&& b, IX&& ix) noexcept
      {
         if (value) {
            serialize<BSON>::template op<Opts>(*value, ctx, b, ix);
         }
         else {
            // Documented level null handling should happen in object serialization
         }
      }
   };

   template <is_variant T>
   struct to<BSON, T>
   {
      template <auto Opts, class Value, is_context Ctx, class B, class IX>
      GLZ_ALWAYS_INLINE static void op(Value&& value, Ctx&& ctx, B&& b, IX&& ix) noexcept
      {
         const auto start_ix = ix;
         ix += 4;
         static constexpr auto ids = ids_v<T>;
         const auto index = value.index();
         const auto& id = ids[index];

         std::visit([&](auto&& v) {
            using V = std::decay_t<decltype(v)>;
            dump(static_cast<char>(bson_type_v<V>), b, ix);
            dump(id, b, ix);
            dump('\0', b, ix);
            serialize<BSON>::template op<Opts>(v, ctx, b, ix);
         }, value);

         dump('\0', b, ix);
         const uint32_t size = static_cast<uint32_t>(ix - start_ix);
         detail::write_bson_size(size, b, start_ix);
      }
   };

   template <class T>
      requires glaze_object_t<T> || reflectable<T> || writable_map_t<T>
   struct to<BSON, T>
   {
      template <auto Opts, class B, class IX>
      static void op(auto&& value, is_context auto&& ctx, B& b, IX& ix) noexcept
      {
         const auto start_ix = ix;
         ix += 4; // space for size

         if constexpr (glaze_object_t<T> || reflectable<T>) {
            static constexpr auto N = reflect<T>::size;
            for_each<N>([&]<size_t I>() {
               static constexpr auto key = reflect<T>::keys[I];
               if constexpr (glaze_object_t<T>) {
                  static constexpr auto member_ptr = get<I>(reflect<T>::values);
                  auto&& member = get_member(value, member_ptr);
                  using V = std::decay_t<decltype(member)>;

                  if constexpr (nullable_like<V>) {
                      if (!member) return;
                  }

                  dump(static_cast<char>(bson_type_v<V>), b, ix);
                  dump(key, b, ix);
                  dump('\0', b, ix);
                  serialize<BSON>::template op<Opts>(member, ctx, b, ix);
               }
               else {
                  auto&& member = get<I>(to_tie(value));
                  using V = std::decay_t<decltype(member)>;

                  if constexpr (nullable_like<V>) {
                      if (!member) return;
                  }

                  dump(static_cast<char>(bson_type_v<V>), b, ix);
                  dump(key, b, ix);
                  dump('\0', b, ix);
                  serialize<BSON>::template op<Opts>(member, ctx, b, ix);
               }
            });
         }
         else {
            for (auto&& [k, v] : value) {
               using V = std::decay_t<decltype(v)>;

               if constexpr (nullable_like<V>) {
                   if (!v) continue;
               }

               dump(static_cast<char>(bson_type_v<V>), b, ix);
               dump(k, b, ix);
               dump('\0', b, ix);
               serialize<BSON>::template op<Opts>(v, ctx, b, ix);
            }
         }

         dump('\0', b, ix);
         const uint32_t size = static_cast<uint32_t>(ix - start_ix);
         detail::write_bson_size(size, b, start_ix);
      }
   };

   template <range T>
      requires(!string_like<T> && !writable_map_t<T>)
   struct to<BSON, T>
   {
      template <auto Opts, class B, class IX>
      static void op(auto&& value, is_context auto&& ctx, B& b, IX& ix) noexcept
      {
         const auto start_ix = ix;
         ix += 4; // space for size

         size_t i = 0;
         char buf[32];
         for (auto&& element : value) {
            using V = std::decay_t<decltype(element)>;
            dump(static_cast<char>(bson_type_v<V>), b, ix);
            char* end_ptr = glz::to_chars(buf, i++);
            dump(std::string_view(buf, end_ptr - buf), b, ix);
            dump('\0', b, ix);
            serialize<BSON>::template op<Opts>(element, ctx, b, ix);
         }

         dump('\0', b, ix);
         const uint32_t size = static_cast<uint32_t>(ix - start_ix);
         detail::write_bson_size(size, b, start_ix);
      }
   };

   template <class T, output_buffer Buffer>
   [[nodiscard]] inline error_ctx write_bson(T&& value, Buffer&& buffer) noexcept
   {
      return write<opts{.format = BSON}>(std::forward<T>(value), std::forward<Buffer>(buffer));
   }

   template <class T>
   [[nodiscard]] inline glz::expected<std::string, error_ctx> write_bson(T&& value) noexcept
   {
      return write<opts{.format = BSON}>(std::forward<T>(value));
   }
}
