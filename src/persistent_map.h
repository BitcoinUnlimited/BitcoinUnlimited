#ifndef BITCOIN_PERSISTENT_MAP_H
#define BITCOIN_PERSISTENT_MAP_H
#include <memory>
#include <stdexcept>
#include <stack>

/*! Simple implementation of a binary tree persistent key value map
  with a somewhat std::-ish interface.

  Note: If access to this data structure is exposed to the network and
  random access should be fast (O(log n)), the key should be a proper
  hash (like TXID) to make it impossible for adversaries to easily
  degenerate the tree into a list!  (Alternatively, improve this
  implementation to be a red-black tree though this is likely not
  necessary)
*/
template <class key_t, class val_t> class persistent_map {
public:
    typedef std::shared_ptr<persistent_map> sptr_pmap;

    /*! Iterate over key, value pairs in key_t order. As the map is
      persistent, only a const iterator (needs to) exist(s).

      TODO: Could maybe be made into a random access iterator using
      the rank functions and some further rework.
    */
    class const_iterator {
    public:
        typedef std::pair<const key_t, const val_t> value_type;
        typedef std::input_iterator_tag iterator_category;
        const_iterator(const persistent_map* p, bool dive_left) {
            if (p != nullptr && p->key != nullptr && p->value != nullptr) {
                todo.push(p);
                if (dive_left)
                    while (todo.top()->left != nullptr)
                        todo.push(todo.top()->left.get());
            }
        }
        value_type operator*() const {
            if (todo.empty())
                throw std::out_of_range("Dereferencing past end of persistent_map.");
            const persistent_map* node = todo.top();
            return value_type(*node->key, *node->value);
        }
        std::shared_ptr<const key_t> key_ptr() const {
            if (todo.empty()) return nullptr;
            const persistent_map* node = todo.top();
            return node->key;
        }

        std::shared_ptr<const val_t> value_ptr() const {
            if (todo.empty()) return nullptr;
            const persistent_map* node = todo.top();
            return node->value;
        }

        bool operator==(const const_iterator& other) const {
            if (todo.empty()) return other.todo.empty();
            else if (other.todo.empty()) return false;
            else return todo.top() == other.todo.top();
        }
        bool operator!=(const const_iterator& other) const {
            return !(*this == other);
        }
        const_iterator operator++() { // prefix operator!
            if (todo.empty())
                throw std::out_of_range("Iteration past end (persistent_map::const_iterator).");
            const persistent_map* node = todo.top();
            todo.pop();
            if (node->right != nullptr) {
                todo.push(node->right.get());
                while (todo.top()->left != nullptr)
                    todo.push(todo.top()->left.get());
            }
            return *this;
        }
    private:
            std::stack<const persistent_map* > todo;
    };

    //! Empty map
persistent_map() : _size(0), key(nullptr), value(nullptr), left(nullptr), right(nullptr) {}

    //! Map with one new entry
persistent_map(const key_t& k, const val_t& v) :
    _size(1),
        key(new key_t(k)),
        value(new val_t(v)),
        left(nullptr),
        right(nullptr) {}


    //! Insert item into map, returning new map.
    persistent_map<key_t, val_t> insert(const key_t& k, const val_t& v) const {
        // insert into empty map
        if (key == nullptr) {
            return persistent_map(k, v);
        }
        // duplicate -> replace this key
        else if (k == *key) {
            return persistent_map(left,
                                  right,
                                  std::shared_ptr<key_t>(new key_t(k)),
                                  std::shared_ptr<val_t>(new val_t(v)));
        }
        else if (k < *key) {
            if (left == nullptr)
                return persistent_map(sptr_pmap(
                                          new persistent_map(k, v)),
                                      right,
                                      key, value);
            else
                return persistent_map(sptr_pmap(
                                          new persistent_map(left->insert(k, v))),
                                      right,
                                      key, value);
        } else { // k > *key
            if (right == nullptr)
                return persistent_map(left,
                                      sptr_pmap(
                                          new persistent_map(k, v)),
                                      key, value);
            else
                return persistent_map(left,
                                      sptr_pmap(
                                          new persistent_map(right->insert(k, v))),
                                      key, value);
        }
    }

    persistent_map<key_t, val_t> remove(const key_t& k) const {
        sptr_pmap new_pmap = remove_internal(k);
        if (new_pmap == nullptr) return persistent_map();
        else return *new_pmap;
    }

    // look up
    // is .end() if not found
    const_iterator at_iter(const key_t& k) const {
        if (key == nullptr) return const_iterator(nullptr, false);
        else if (*key == k) return const_iterator(this, false);
        else if (k < *key) {
            if (left == nullptr) return const_iterator(nullptr, false);
            else return left->at_iter(k);
        } else { // k > *key
            if (right == nullptr) return const_iterator(nullptr, false);
            else return right->at_iter(k);
        }
    }

    // is nullptr if not found
    std::shared_ptr<const val_t> at_ptr(const key_t& k) const {
        return at_iter(k).value_ptr();
    }

    const val_t& at(const key_t& k) const {
        const std::shared_ptr<const val_t> vp = at_ptr(k);
        if (vp == nullptr) throw std::out_of_range("Key not found (at)");
        return *vp;
    }

    bool contains(const key_t& k) const {
        if (key == nullptr) return false;
        else if (*key == k) return true;
        else if (k < *key) {
            if (left == nullptr) return false;
            else return left->contains(k);
        } else {
            if (right == nullptr) return false;
            else return right->contains(k);
        }
    }

    size_t rank_of(const key_t& k) const {
        if (key == nullptr) throw std::out_of_range("Key not found in empty persistent_map (rank_of)");
        else if (*key == k) {
            return size() - 1 - (
                (right == nullptr) ? 0 : right->size());
        } else if (k < *key) {
            if (left == nullptr) throw std::out_of_range("Key not found (rank_of, left)");
            else return left->rank_of(k);
        } else { // k > *key
            if (right == nullptr) throw std::out_of_range("Key not found (rank_of, right)");
            else return 1+(left == nullptr ? 0 : left->size()) + right->rank_of(k);
        }
    }

    const_iterator by_rank(size_t rank) const {
        if (size() <= rank) return end();
        else if (left != nullptr) {
            if (rank<left->size()) return left->by_rank(rank);
            else if (rank == left->size()) return const_iterator(this, false);

            // this should never happen:
            else if (right == nullptr) throw std::out_of_range("Rank out of range (INTERNAL ERROR 1)");
            else return right->by_rank(rank - left->size() - 1);
        } else {
            if (rank == 0) return const_iterator(this, false);
            // this should never happen either:
            else if (right == nullptr) throw std::out_of_range("Rank out of range (INTERNAL ERROR 2)");
            else return right->by_rank(rank - 1);
        }
    }

    //! Return number of items in map
    size_t size() const { return _size; }
    //! Map is empty (size = 0)?
    bool empty() const { return size() == 0; }

    const_iterator begin() const { return const_iterator(this, true); }
    const_iterator end() const { return const_iterator(nullptr, false); }
private:
    sptr_pmap remove_internal(const key_t& k) const {
        if (key == nullptr)
            throw std::out_of_range("Cannot remove from empty persistent_map.");

        if (k < *key) {
            if (left == nullptr)
                throw std::out_of_range("Key not found while removing from persistent_map (LHS)");
            else return sptr_pmap(new persistent_map(left->remove_internal(k), right, key, value));
        }
        else if (*key < k) {
            if (right == nullptr)
                throw std::out_of_range("Key not found while removing from persistent_map (RHS)");
            else return sptr_pmap(new persistent_map(left, right->remove_internal(k), key, value));
        }

        // k == *key

        if (left == nullptr && right == nullptr) {
            return nullptr;
        } else if (left == nullptr) {
            return right;
        } else if (right == nullptr) {
            return left;
        } else {
            // both children there. If left size is equal to or bigger
            // than right size, find immediate predecessor and place
            // here, else do the same for the immediate successor

            if (left->size() >= right->size()) {
                sptr_pmap imm_pred = left;
                while (imm_pred->right != nullptr) imm_pred = imm_pred->right;
                return sptr_pmap(new persistent_map(left->remove_internal(*imm_pred->key),
                                                    right,
                                                    imm_pred->key, imm_pred->value));
            } else {
                sptr_pmap imm_succ = right;
                while (imm_succ->left != nullptr) imm_succ = imm_succ->left;
                return sptr_pmap(new persistent_map(left,
                                                    right->remove_internal(*imm_succ->key),
                                                    imm_succ->key, imm_succ->value));
            }
        }
    }

    //! internally used constructor
    explicit persistent_map(const sptr_pmap _left,
                            const sptr_pmap _right,
                            const std::shared_ptr<key_t> _key,
                            const std::shared_ptr<val_t> _value) :
    _size(1), key(_key), value(_value), left(_left), right(_right) {
        if (left != nullptr) _size  += left->size();
        if (right != nullptr) _size += right->size();
    }

    /*! number of items in this node plus left and right subtrees. A value of
      zero indicates that this node is empty. */
    size_t _size;
    std::shared_ptr<key_t> key;
    std::shared_ptr<val_t> value;

    // Left and right subtrees. Can be nullptr if not existent
    sptr_pmap left, right;
};

struct persistent_set_none_type {};

/*! Set based on partial_map. Note that this isn't optimal yet as it
  carries around a value pointer as well exposing the value-accessing
  functionality still. */
template<class key_t> class persistent_set : persistent_map<key_t, persistent_set_none_type>{
    /* FIXME: Hide (make private) methods in persistent_map and in
       persistent_map::const_iterator that do not make sense for a set. */
};

#endif
