# Lock Free Cache
A Lazy Lock-Free Least Recently Used (LRU) Cache

In a majority of programs, functions will be called more than once with the same inputs resulting in the same outputs. Often this can present a bottleneck and a time-space trade-off is to store inputs and outputs in a cache. As more function mappings are cached, the cache grows without bound. Therefore a strategy must be in place to evict mappings that are least useful, of which there are many. The least recently used (LRU) cached value is often a good proxy for 'least useful'.

LRU Cache

To achieve O(1) access, we use a hashmap, inserting nodes from the head at tail of each buckets' list. To achieve O(1) eviction, we use an atomic counter and delete any node observed that is past its use by date. Note that this differs from the usual strategy of connecting nodes with a doubly linked list, which would require unnecessary overhead and extraordinary care. Some garbage (average O(n) space) is left behind but does not build up or leak over time. I have called this clean up 'lazy'. Note that the goal of a cache is to provide a time-space trade-off.
Hashbuckets are determined from the function inputs, and nodes in each bucket contain the values of the function inputs and their outputs. It is worth keeping in mind that there are more cache-coherent solutions, with worse big-O scaling that perform better. Linked-lists are slow.

The interface includes a constructor which takes a lambda and deduces all template arguments, a destructor and a call method `operator()`. This allows users to treat the cache with the same syntax as the lambda. This looks up the inputs in the map and returns the value, moving the node down the map and updating its counter. If the value is not found, the value is computed, and stored in the map with the value of the counter when it was created.

Lock-Based Concurrency

Where multiple threads might require the same result, it is desireable for multiple threads to access the same data structure. This is an open problem. A quick and dirty solution is to have a global lock on the data structure. This has the drawback that any thread that is scheduled out while holding the mutex will prevent other threads from making any progress. Finer grained locking will therefore improve performance when there is more contention. However this is still lock-based solution.

Lock-Free Concurrency

The alternative is a lock-free data structure. 'Lock-freedom' is the property that even for pathological thread scheduling, some thread will make progress. Under high contention, these significantly outperform their lock-based counterparts. Lock-free data structures rely on read-modify-write atomics. These include fetch-and-add (FAA) operations and compare-and-swap (CAS) operations.

On x86, CAS operations are built on the `lock cmpxchg8b` and `lock cmpxchg16b` instructions. This operation atomically stores a value when it matches an expected value, otherwise fails. This is a flexible sledgehammer. By looping on failure, no scheduled-out thread can prevent progress; this is lock-freedom.

FAA operations have the advantage over CAS operations that they never need to loop and retry, and can be used to produce 'wait-free' structures. These have the very strong guarantee that all running threads always make progress. Wait-freedom is a vibrant area of modern theoretical computer science research.

ABA Problem and Dereferencing freed memory.

A value is read from shared memory, and then manipulated to determine a new value to be swapped in with CAS. A CAS operation only checks that a value is still the same, not that nothing has changed. For example a pointer may have been modified to point to something new, the old pointer freed, and a new object created pathologically at the same place on the heap.

Another issue is that two threads may load the same atomic pointer and one may free the object before another dereferences the pointer. This results in undefined behaviour. There needs to be a mechanism whereby each node is deleted only when it is inaccessible. The solution to both of these problems is a split reference count.

Split reference counts

Pointers are packed in an atomic struct with a reference count, and nodes have their own atomic struct packing two reference counts, internal and external. Before dereferencing a pointer, the pointer's reference count is incremented. When the dereferenced node is no longer being read, the struct's internal reference count is incremented. When a pointer detaches from a node, the pointer's count is subtracted from the internal count, and the node's internal count is decreased by 1. When both of a node's reference counts are observed by a thread to reach zero, the node is deleted by that thread. All these operations are implemented with CAS.
This mechanism has then been packaged as an atomic shared pointer class.

Concurrent deletions

Consider a linked list Head->A->B->C->D->E.

If B and C are removed concurrently, we might observe atomic pointers swing to:

  Head->A->C (B->D->E deleted recursively)
  
not:

  Head->A->D->E.
  
  We do not want the wrong nodes deleted.

The solution is as follows:

To delete node B, mark B's pointer to C. B is now 'logically deleted.' This pointer can no longer be modified. The pointer from A is now reassigned to point to the first node that is not logically deleted. This is a strategy devised by Tim Harris.

It is difficult to logically delete a node and then logically undelete the node without introducing race conditions. Therefore the data in a logically deleted node must be extracted and placed into a new node. The data must be held by a const or atomic shared pointer. This can be implemented with the same split reference counting system.

Hash collisions

If two different values fall into the same hash bucket, then a linked list must be formed. Since it is rare for a bucket to contain more than 3 or 4 nodes, it is desireable to make this bucket as simple as possible. Here a linked list has been used, where new values are appended at the tail and removed from the head. Notably there is no tail pointer, and the tail is accessed by traversing the list from the head. This has the benefit that a thread that traverses a full hash bucket and does not find the node it is looking for can determine that it does not exist, and has not been inserted higher up the bucket. Furthermore, this provides a mechanism to increase the garbage collected.
