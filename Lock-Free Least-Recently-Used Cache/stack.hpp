// stack
// not part of final project
// but used a lot to test atomic pointers
#ifndef stack_hpp
#define stack_hpp

#include <atomic>
#include "harris_ptr.hpp"

namespace frd {
    template<typename T>
    class stack
    {
    private:
        struct node {
            frd::shared_ptr<T> data;
            frd::shared_ptr<node> next;
            node(T const& data_):
            data(frd::make_shared<T>(data_)) {}
            ~node() {}
        };
        frd::atomic_shared_ptr<node> head;
    public:
        void push(T const& data)
        {
            frd::shared_ptr<node> new_node = frd::make_shared<node>(data);
            new_node->next=head.load();
            while(!head.compare_exchange_weak(new_node->next,new_node));
        }
        frd::shared_ptr<T> pop()
        {
            frd::shared_ptr<node> old_head= head.load();
            while(old_head && ! head.compare_exchange_weak(old_head, old_head->next));
            return old_head ? old_head->data : frd::shared_ptr<T>();
        }
        ~stack() {
            while(pop());
        }
        
    };
} /* namespace */

#endif /* stack_hpp */
