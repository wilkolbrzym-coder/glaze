// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <cstdint>
#include <cstring>
#include <bit>
#include <string_view>
#include <type_traits>
#include <algorithm>
#include "glaze/core/common.hpp"
#include "glaze/util/dump.hpp"

namespace glz
{
   // BSON Type Tags
   enum struct bson_type : uint8_t {
      Double = 0x01,
      String = 0x02,
      Document = 0x03,
      Array = 0x04,
      Binary = 0x05,
      Undefined = 0x06, // Deprecated
      ObjectId = 0x07,
      Boolean = 0x08,
      DateTime = 0x09,
      Null = 0x0A,
      Regex = 0x0B,
      DBPointer = 0x0C, // Deprecated
      JavaScript = 0x0D,
      Symbol = 0x0E, // Deprecated
      CodeWithScope = 0x0F,
      Int32 = 0x10,
      Timestamp = 0x11,
      Int64 = 0x12,
      Decimal128 = 0x13,
      MinKey = 0xFF,
      MaxKey = 0x7F
   };

   namespace detail {
      template <class T>
      struct bson_type_helper {
         static constexpr uint8_t get() noexcept {
            using V = std::decay_t<T>;
            if constexpr (std::same_as<V, double>) return uint8_t(bson_type::Double);
            else if constexpr (std::same_as<V, float>) return uint8_t(bson_type::Double);
            else if constexpr (str_t<V>) return uint8_t(bson_type::String);
            else if constexpr (is_named_enum<V>) return uint8_t(bson_type::String);
            else if constexpr (glaze_object_t<V> || reflectable<V> || writable_map_t<V> || readable_map_t<V>) return uint8_t(bson_type::Document);
            else if constexpr (is_variant<V>) return uint8_t(bson_type::Document);
            else if constexpr (range<V>) return uint8_t(bson_type::Array);
            else if constexpr (bool_t<V>) return uint8_t(bson_type::Boolean);
            else if constexpr (std::same_as<V, int32_t>) return uint8_t(bson_type::Int32);
            else if constexpr (std::integral<V> && sizeof(V) <= 4) return uint8_t(bson_type::Int32);
            else if constexpr (std::integral<V> && sizeof(V) == 8) return uint8_t(bson_type::Int64);
            else if constexpr (std::is_enum_v<V>) return uint8_t(bson_type::Int32);
            else if constexpr (nullable_like<V>) {
                return bson_type_helper<typename V::value_type>::get();
            }
            else return uint8_t(bson_type::Null);
         }
      };
   }

   template <class T>
   inline constexpr uint8_t bson_type_v = detail::bson_type_helper<T>::get();

   namespace detail {
      template <typename T>
      GLZ_ALWAYS_INLINE constexpr T swap_bytes(T val) noexcept {
          auto* ptr = reinterpret_cast<uint8_t*>(&val);
          for (size_t i = 0; i < sizeof(T) / 2; ++i) {
              std::swap(ptr[i], ptr[sizeof(T) - 1 - i]);
          }
          return val;
      }
   }

   // BSON is little-endian
   template <class T, class B>
   GLZ_ALWAYS_INLINE void dump_le(const T value, B& b, size_t& ix) noexcept
   {
      static constexpr auto n = sizeof(T);
      if constexpr (std::endian::native == std::endian::little) {
         if constexpr (vector_like<B>) {
            if (ix + n > b.size()) [[unlikely]] {
               b.resize(std::max(b.size() * 2, ix + n));
            }
         }
         std::memcpy(&b[ix], &value, n);
      }
      else {
         T swapped = detail::swap_bytes(value);
         if constexpr (vector_like<B>) {
            if (ix + n > b.size()) [[unlikely]] {
               b.resize(std::max(b.size() * 2, ix + n));
            }
         }
         std::memcpy(&b[ix], &swapped, n);
      }
      ix += n;
   }

   template <class T, class It>
   GLZ_ALWAYS_INLINE T read_le(It& it) noexcept
   {
      T value;
      std::memcpy(&value, &(*it), sizeof(T));
      it += sizeof(T);
      if constexpr (std::endian::native == std::endian::little) {
         return value;
      }
      else {
         return detail::swap_bytes(value);
      }
   }
}
