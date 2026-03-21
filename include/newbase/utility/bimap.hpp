#include <unordered_map>
#include <optional>

namespace nb {

    template <typename A, typename B>
    class bimap
    {
    public:
        using a_to_b_map = std::unordered_map<A, B>;
        using b_to_a_map = std::unordered_map<B, A>;

        void insert(const A &a, const B &b)
        {
            // maintain bijection: remove previous mappings if they conflict
            auto it_a = a_to_b.find(a);
            if (it_a != a_to_b.end()) {
                if (it_a->second != b) {
                    b_to_a.erase(it_a->second);
                } else {
                    // same mapping already present
                    return;
                }
            }

            auto it_b = b_to_a.find(b);
            if (it_b != b_to_a.end()) {
                if (it_b->second != a) {
                    a_to_b.erase(it_b->second);
                } else {
                    // same mapping already present
                    return;
                }
            }

            a_to_b[a] = b;
            b_to_a[b] = a;
        }

        bool contains_a(const A &a) const
        {
            return a_to_b.find(a) != a_to_b.end();
        }

        bool contains_b(const B &b) const
        {
            return b_to_a.find(b) != b_to_a.end();
        }

        std::optional<B> get_b(const A &a) const
        {
            auto it = a_to_b.find(a);
            if (it == a_to_b.end()) return std::nullopt;
            return it->second;
        }

        std::optional<A> get_a(const B &b) const
        {
            auto it = b_to_a.find(b);
            if (it == b_to_a.end()) return std::nullopt;
            return it->second;
        }

        const B &at_a(const A &a) const
        {
            return a_to_b.at(a);
        }

        B &at_a(const A &a)
        {
            return a_to_b.at(a);
        }

        const A &at_b(const B &b) const
        {
            return b_to_a.at(b);
        }

        A &at_b(const B &b)
        {
            return b_to_a.at(b);
        }

        bool erase_a(const A &a)
        {
            auto it = a_to_b.find(a);
            if (it == a_to_b.end()) return false;
            B mapped = it->second;
            a_to_b.erase(it);
            b_to_a.erase(mapped);
            return true;
        }

        bool erase_b(const B &b)
        {
            auto it = b_to_a.find(b);
            if (it == b_to_a.end()) return false;
            A mapped = it->second;
            b_to_a.erase(it);
            a_to_b.erase(mapped);
            return true;
        }

        void clear()
        {
            a_to_b.clear();
            b_to_a.clear();
        }

        std::size_t size() const
        {
            return a_to_b.size();
        }

        bool empty() const
        {
            return a_to_b.empty();
        }

        const a_to_b_map &map_a_to_b() const { return a_to_b; }
        const b_to_a_map &map_b_to_a() const { return b_to_a; }

    private:
        a_to_b_map a_to_b;
        b_to_a_map b_to_a;
    };

}