#pragma once

#include <ratio>

namespace my_log {
    template <typename _Rep, typename _Capacity = std::ratio<1>>
    class space;

    template <typename _Tospace, typename _Rep, typename _Capacity>
    constexpr _Tospace space_cast(const space<_Rep, _Capacity>& d) {
        auto _r = d.count() * std::ratio_divide<_Capacity, _Tospace::period>::num /
        std::ratio_divide<_Capacity, _Tospace::period>::den;
        return _Tospace(_r);
    }

    template <typename _Rep, typename _Capacity>
    class space {
    public:
        using rep = _Rep;
        using period = _Capacity;
        
        constexpr space() : _rep() {}

        template <typename _Rep2>
        explicit space(const _Rep2& _r) : _rep(_r) {}

        template <typename _Rep2, typename _Capacity2>
        constexpr space(const space<_Rep2, _Capacity2>& _d) : _rep(space_cast<space>(_d).count()) {}
        
        constexpr _Rep count() const {
            return _rep;
        }

        constexpr space operator+() const {
            return *this;
        }

        constexpr space operator-() const {
            return space(-count());
        }

        space operator++(int) {
            space _tmp(*this);
            ++_rep;
            return _tmp;
        }

        space& operator++() {
            ++_rep;
            return *this;
        }

        space& operator--() {
            --_rep;
            return *this;
        }

        space operator--(int) {
            space temp(*this);
            _rep--;
            return temp;
        }

        space& operator+=(const space& _d) {
            _rep += _d.count();
            return *this;
        }

        space& operator-=(const space& _d) {
            _rep -= _d.count();
            return *this;
        }

        space& operator*=(const _Rep& _d) {
            _rep *= _d;
            return *this;
        }

        space& operator/=(const _Rep& _d) {
            _rep /= _d;
            return *this;
        }

        space& operator%=(const _Rep& _d) {
            _rep %= _d;
            return *this;
        }


    private:
        _Rep _rep;
};
    template <typename _Rep, typename _Capacity>
        space<_Rep, _Capacity> operator+(space<_Rep, _Capacity>& _a, space<_Rep, _Capacity>& _b) {
            return space<_Rep, _Capacity>(_a.count() + _b.count());
        }

        template <typename _Rep, typename _Capacity>
        space<_Rep, _Capacity> operator-(space<_Rep, _Capacity>& _a, space<_Rep, _Capacity>& _b) {
            return space<_Rep, _Capacity>(_a.count() - _b.count());
        }

        using kilo = std::ratio<1024L, 1>;
        using mega = std::ratio<1024L * 1024, 1>;
        using giga = std::ratio<1024L * 1024 * 1024 , 1>;
        using tera = std::ratio<1024ULL * 1024 * 1024 * 1024, 1>;

        using bytes = space<size_t>;
        using kilobytes = space<size_t, kilo>;
        using megabytes = space<size_t, mega>;
        using gigabytes = space<size_t, giga>;
        using terabytes = space<size_t, tera>;
}