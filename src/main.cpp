#include <cstdio>
#include "order_book.hpp"

int main()
{
    OrderBook<1024, 65536> book(100, 1);

    book.submit_order(1, Side::BUY, 105, 10, 1000);
    book.submit_order(2, Side::SELL, 110, 5, 1001);

    if (book.has_bid())
    {
        std::printf("best bid: %llu x %llu\n",
                    static_cast<unsigned long long>(book.best_bid_price()),
                    static_cast<unsigned long long>(book.best_bid_quantity()));
    }
    if (book.has_ask())
    {
        std::printf("best ask: %llu x %llu\n",
                    static_cast<unsigned long long>(book.best_ask_price()),
                    static_cast<unsigned long long>(book.best_ask_quantity()));
    }

     return 0;
}
