// glz:header path="glaze/util/memory_pool.hpp"
// glz:header project_imports=ignore
module;

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>
// glz:emit std
export module glaze.util.memory_pool;

import std;
import glaze.core.basic_types;

export namespace glz
{
   template <class T>
      requires(std::is_default_constructible_v<T>)
   struct memory_pool final
   {
      memory_pool() = default;

      // Disable copy and move semantics
      memory_pool(const memory_pool&) = delete;
      memory_pool& operator=(const memory_pool&) = delete;
      memory_pool(memory_pool&&) = delete;
      memory_pool& operator=(memory_pool&&) = delete;

      // Ensures that release is called exactly once per acquired object
      struct handle final
      {
         memory_pool<T>* pool;
         glz::size_t index;

         handle(memory_pool<T>* p, glz::size_t i) : pool(p), index(i) {}
         ~handle() { pool->release(index); }
      };

      std::shared_ptr<T> borrow()
      {
         std::lock_guard lock{mtx};
         if (available.empty()) {
            const auto current_size = values.size();
            const auto new_size = values.size() * 2;
            values.resize(new_size);
            for (glz::size_t i = current_size; i < new_size; ++i) {
               available.emplace_back(i);
            }
         }

         const auto index = available.back();
         available.pop_back();

         // Use an aliasing constructor with a handle that will return the value
         // to the pool
         return std::shared_ptr<T>{std::make_shared<handle>(this, index), &values[index]};
      }

      glz::size_t size() const
      {
         std::lock_guard lock{mtx};
         return values.size();
      }

      glz::size_t available_size() const
      {
         std::lock_guard lock{mtx};
         return available.size();
      }

     private:
      void release(glz::size_t index)
      {
         std::lock_guard lock{mtx};
         available.emplace_back(index);
      }

      mutable std::mutex mtx{};
      std::deque<T> values{2};
      std::vector<glz::size_t> available{0, 1}; // indices of available values
   };
}
