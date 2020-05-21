//
// This is a least recently used cache, based on a hash map and 'time'-stamped nodes.
// Objects are inserted in O(1) time and take up O(n) space.
// Unused objects are marked as garbage in O(1) time.
// Garbage is deleted in O(1) time and takes up O(n) space where n is the size of the cache.
//

#ifndef LRU_cache_hpp
#define LRU_cache_hpp

#include <atomic>
#include <array>
#include "harris_ptr.hpp"

std::atomic<long> leaks (0); // debug only

namespace frd {
    template <  class K,
                class V,
                class Lambda,
                class Hasher = std::hash<K>,
                long buckets = 100,
                long node_life = buckets
    >
    class cache {
    private:
        struct KV {
            K k;
            V v;
        };
        struct node {
            const frd::shared_ptr<KV> data;
            frd::atomic_shared_ptr<node> next;
            const long time_stamp;
            ~node() {leaks--;}
            node(frd::shared_ptr<KV> data_, long local): data(data_), time_stamp(local) {leaks++;}
        };
        
        class cursor {
            frd::shared_ptr<node> owner_holder;
            frd::atomic_shared_ptr<node>* owner;
            frd::shared_ptr<node> current;
            long local_time;
        public:
            cursor(frd::atomic_shared_ptr<node>* head_, long local)
            : owner_holder(nullptr), owner(head_), current(head_->load()), local_time(local) {
                if(current.get_mark()) go_next();
            }
            bool go_next() {
                while(current) {
                    owner_holder = current;
                    owner = &owner_holder->next;
                    current = owner->load();
                    if(current.get_mark() or
                       current?(current->time_stamp+node_life < local_time):false)
                        remove();
                    else break;
                }
                return !!current;
            }
            
            bool remove() {
                if(!current) return false;
                current->next.mark(true);
                auto curr = owner->load();
                if(curr.get_mark() or curr!=current) return false;
                auto next = curr->next.load();
                while(next) {
                    auto next_next_ptr = next->next.load();
                    if(next_next_ptr.get_mark()) next = next_next_ptr;
                    else break;
                }
                return owner->compare_exchange_weak(curr, next); // yes weak
            }
            
            frd::shared_ptr<node> get() {
                return current;
            }
            
            bool push_end(frd::shared_ptr<node> val) {
                for(;;) {
                    if(current.get_mark()) return false;
                    if(owner->compare_exchange_strong(current,val)) return true;
                }
            }
        };
        
        Lambda fnc;
        Hasher hsh;
        std::array<frd::atomic_shared_ptr<node>,buckets> head_buckets;
        std::atomic<long> time;
        
        frd::shared_ptr<node> push_end(frd::shared_ptr<KV> sp, long bucket, long local_time) {
            auto n = frd::make_shared<node>(sp, local_time);
            for(;;) {
                cursor c (&head_buckets[bucket], local_time);
                while(c.go_next());
                if(c.push_end(n)) return n;
            }
        }
        
        frd::shared_ptr<node> pop_first(frd::shared_ptr<KV> sp,long bucket, long local_time) {
            cursor c (&head_buckets[bucket],local_time);
            while(c.get() and c.get()->data != sp) {c.go_next();}
            c.remove();
            return c.get();
        }
        
    public:
        cache(Lambda func,Hasher hash = std::hash<K>()) : fnc(func), hsh(hash), time(0) {
            std::fill(head_buckets.begin(),head_buckets.end(),nullptr);
        }
        
        V operator()(K const& data) {
            auto local_time = time.fetch_add(1);
            auto bucket = hsh(data)%buckets;
            cursor c (&head_buckets[bucket],local_time);
            while(c.get()) {
                if(c.get()->data->k == data) {
                    auto node = push_end(c.get()->data, bucket, local_time);
                    pop_first(node->data, bucket, local_time);
                    return c.get()->data->v;
                } else { c.go_next(); }
            }
            V out = fnc(data);
            KV kvr = {data,out};
            push_end(frd::make_shared<KV>(kvr), bucket, local_time);
            return out;
        }
    };
    
    namespace detail {
        template<class Ld>
        struct lambda_type : lambda_type<decltype(&Ld::operator())> {}; // CRTP
        template<class Ret, class Cls, class Arg>
        struct lambda_type<Ret(Cls::*)(Arg) const> {
            using return_type = Ret;
            using arg_type = Arg;
        };
    }
    
    template<class Lambda> cache(Lambda) -> // CTAD
    cache<
        typename detail::lambda_type<Lambda>::arg_type,
        typename detail::lambda_type<Lambda>::return_type,
        Lambda
    >;
}

#endif /* LRU_cache_hpp */
