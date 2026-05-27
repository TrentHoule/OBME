#include <fstream>
#include <sstream>
#include "orderBook.h"

/*
This file will be for reading in and processing the sample data from LOBSTER

Steps in the process:
1. read the file
2. create a trade for each line and process it
3. Save each order / trade to an output file
*/

void readOrdersAndProcess(std::string filename) {
    OrderBook<Quantity> APPL{};
    std::unordered_map<Id, uint> idsMap;

    std::ifstream file(filename);
    if(file.is_open())
    {
        std::string line;
        while(getline(file, line))
        {
            std::stringstream ss(line);
            std::string token;
            std::vector <std::string> temp;
            
            /*
            This grabs: time, type, id, quantity, price, and direction.
            We want to:
                - ignore time
                - only get type of 1, 2, and 3 (addOrder, modification, deletion respectively)
                - save a map of their id to returned ids when an order is added so we know which one to cancel
                - get the quantity of shares
                - get the price
                - know if it is a buy or sell order

            */ 
            while(getline(ss, token, ','))
            {
                temp.push_back(token);
            }

            if (std::stoi(temp[1]) == 1) {
                auto type = OrderType::goodTillCanceled;
                Side side = (std::stoi(temp[5]) == 1) ? Side::Bid : Side::Ask;
                Price price = std::stoi(temp[4]);
                Quantity shares = std::stoi(temp[3]);
                auto theirId = std::stoi(temp[2]);

                // std::cout << std::format("Added {}", theirId) << std::endl;

                Id id = APPL.addOrder(type, side, price, shares);
                idsMap.emplace(theirId, id);

            } 
            // Handle deletion
            else if (std::stoi(temp[1]) == 3) {
                auto theirId = std::stoi(temp[2]);
                // std::cout << std::format("Canceled order {}", theirId) << std::endl;
                auto it = idsMap.find(theirId);
                if (it != idsMap.end()) {
                    APPL.cancelOrder(it->second);
                }
            }
            // else if (std::stoi(temp[1]) == 2) {
            //     Price price = std::stoi(temp[4]);
            //     Quantity shares = std::stoi(temp[3]);
            //     auto theirId = std::stoi(temp[2]);

            //     std::cout << std::format("Added {}", theirId) << std::endl;

            //     Id id = APPL.modifyOrder(theirId, price, shares);
            //     idsMap.emplace(theirId, id);
            // }
        }

        // APPL.printAsks();
        // std::cout << std::endl;
        // APPL.printBids();

        file.close();
    }

}

int main() {
    
    auto filename = "SampleData/AAPL_2012-06-21_34200000_57600000_message_1.csv";

    readOrdersAndProcess(filename);
    
    
    
    
    return 0;
}