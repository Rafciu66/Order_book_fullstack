//#pragma once // Prevents the header from being included multiple times

#include <vector>      // <-- ADD THIS for std::vector
#include <unordered_map> // <-- ADD THIS for std::unordered_map
#include <iostream>
#include <cstddef>
#include <utility>
#include <cstdint> // For uint types
#include <iostream>
#include <fstream>   // For file input/output
#include <string>    // For using std::string
#include <sstream>   // For using std::stringstream to parse strings

struct Order {
    uint64_t order_id;
    uint32_t quantity;
    uint32_t price;
    bool is_bid;

    Order* next;
    Order* prev;

    Order(uint64_t id, uint32_t p, uint32_t q, bool side)
        : order_id(id), price(p), quantity(q), is_bid(side), next(nullptr), prev(nullptr) {
    }

    ~Order() {}
};

union StorageSlot {
    Order order_data;

    struct FreeListNode {
        StorageSlot* next;
	} free_list_node;
    
    #pragma warning(suppress : 26495)
    StorageSlot() {}
    ~StorageSlot() {}

};


template <size_t MaxCount>
class OrderAllocator { // Pool Allocator
    // The entire block of memory, correctly typed as StorageSlot.
    StorageSlot memory_pool[MaxCount];

    // The head of the free list is a pointer to a free StorageSlot.
    StorageSlot* free_list_head;

public:
    OrderAllocator() : free_list_head(&memory_pool[0]) {
        // Build the rest of the list, starting from the new head.
        for (size_t i = 0; i < MaxCount - 1; ++i) {
            memory_pool[i].free_list_node.next = &memory_pool[i + 1];
        }
        // The tail of the list points to null
        memory_pool[MaxCount - 1].free_list_node.next = nullptr;
    }

	template <typename... Args>
    Order* allocate(Args&&... args) {
        if (!free_list_head) {
			return nullptr; // Pool exhausted
        }

        // 1. Pop a slot from the free list
        StorageSlot* slot = free_list_head;
		free_list_head = free_list_head->free_list_node.next;

        // 2. Use placement new on the 'order_data' member of the union.
        // This constructs an Order object inside the slot, "flipping" its identity.
        Order* result = new (&(slot->order_data)) Order(std::forward<Args>(args)...);
		return result;
    }

    void deallocate(Order* p) {
        if (!p) return;
        
        // 1. Call the destructor of the Order object.
        p->~Order();
        // 2. Cast the Order pointer back to a StorageSlot pointer.
        StorageSlot* slot = reinterpret_cast<StorageSlot*>(p);
        // 3. Push the slot back onto the free list.
        slot->free_list_node.next = free_list_head;
		free_list_head = slot;
    }
};

// A structure to hold aggregate information at a price level.
struct PriceLevel {
    uint64_t total_volume = 0;
    uint32_t order_count = 0;
    Order* head = nullptr;
    Order* tail = nullptr;
};

void process_order_file(const std::string& filename) {
    // 1. Open the file for reading.
    std::ifstream file(filename);

    // 2. Check if the file was successfully opened.
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file '" << filename << "'" << std::endl;
        return; // Exit the function if file opening fails.
    }

    std::cout << "Successfully opened " << filename << ". Starting processing..." << std::endl;
    std::string line;
    int line_number = 0;

    // 3. Read the file line by line until the end.
    while (std::getline(file, line)) {
        line_number++;

        // Use a stringstream to easily parse words from the line.
        std::stringstream ss(line);
        std::string action;

        // Extract the first word, which is the action.
        ss >> action;

        // Skip empty lines or lines that start with '#' (comments).
        if (action.empty() || action[0] == '#') {
            continue;
        }

        // 4. Use an if-else if block to handle the different commands.
        if (action == "ADD") {
            std::string side;
            uint64_t order_id;
            uint32_t price;
            uint32_t quantity;

            // Try to extract the remaining arguments for an ADD command.
            if (ss >> side >> order_id >> price >> quantity) {
                std::cout << "Line " << line_number << ": Parsed ADD. Side: " << side
                    << ", ID: " << order_id << ", Price: " << price
                    << ", Qty: " << quantity << std::endl;
                // --- In your real app, you would call your order book here ---
                // e.g., order_book.add_order(order_id, price, quantity, (side == "BID"));
            }
            else {
                std::cerr << "Line " << line_number << ": Malformed ADD command." << std::endl;
            }

        }
        else if (action == "CANCEL") {
            uint64_t order_id;
            if (ss >> order_id) {
                std::cout << "Line " << line_number << ": Parsed CANCEL. ID: " << order_id << std::endl;
                // --- Call your order book here ---
                // e.g., order_book.cancel_order(order_id);
            }
            else {
                std::cerr << "Line " << line_number << ": Malformed CANCEL command." << std::endl;
            }

        }
        else if (action == "MODIFY") {
            uint64_t order_id;
            uint32_t new_quantity;
            if (ss >> order_id >> new_quantity) {
                std::cout << "Line " << line_number << ": Parsed MODIFY. ID: " << order_id
                    << ", New Qty: " << new_quantity << std::endl;
                // --- Call your order book here ---
                // e.g., order_book.modify_order(order_id, new_quantity);
            }
            else {
                std::cerr << "Line " << line_number << ": Malformed MODIFY command." << std::endl;
            }

        }
        else if (action == "PRINT") {
            std::cout << "\n=============================================" << std::endl;
            std::cout << "Line " << line_number << ": Parsed PRINT command. (Printing book state)" << std::endl;
            std::cout << "=============================================\n" << std::endl;
            // --- Call your order book here ---
            // e.g., order_book.print_book();

        }
        else {
            std::cerr << "Line " << line_number << ": Unknown command '" << action << "'" << std::endl;
        }
    }

    std::cout << "Finished processing file." << std::endl;
}

template<size_t MaxOrders, uint32_t MinPrice, uint32_t MaxPrice>
class OrderBook {
private:
    // Our custom allocator is a member of the class.
    OrderAllocator<MaxOrders> allocator_;

    // Direct-mapped arrays for price levels.
    std::vector<PriceLevel> bids_;
    std::vector<PriceLevel> asks_;

    // O(1) lookup for orders by their ID.
    std::unordered_map<uint64_t, Order*> order_map_;

    const uint32_t min_price_ = MinPrice;
    const uint32_t price_range_ = MaxPrice - MinPrice + 1;

public:
    OrderBook() {
        // Pre-size the vectors to avoid reallocations during runtime.
        bids_.resize(price_range_);
        asks_.resize(price_range_);
    }

    void add_order(uint64_t order_id, uint32_t price, uint32_t quantity, bool is_bid) {
        // --- LOGIC TO IMPLEMENT ---
        // 1. Check if order ID already exists in order_map_. If so, reject.
        
        if (order_map_.count(order_id)) {
            // In a real system, you'd log this error.
            return;
        }
        if (price < min_price_ || price > MaxPrice) {
            return;
        }
        
        // 2. Use allocator_.allocate(...) to create a new Order object.
        Order* new_order = allocator_.allocate(order_id, price, quantity, is_bid);
        if (new_order == nullptr) {
            std::cerr << "Out of memory in allocator!" << std::endl;
            return;
        }
        // 3. Determine if it's a bid or ask and get the correct price level from bids_ or asks_.
        //    - Use an index: `size_t index = price - min_price_;`

        std::vector<PriceLevel>& side_vec = is_bid ? bids_ : asks_;
        size_t index = price - min_price_;
        PriceLevel& level = side_vec[index];

        // 4. Update the PriceLevel's total_volume and order_count.
        // 5. Add the new Order to the end of the doubly-linked list at that PriceLevel.
        //    - Update head/tail pointers correctly (especially for the first order).
        if (level.head == nullptr) {
            // CASE 1: The list for this price was empty.
            level.head = new_order;
            level.tail = new_order;
        }
        else {
            // CASE 2: The list was not empty. Add to the back.
            level.tail->next = new_order;
            new_order->prev = level.tail;
            level.tail = new_order;
        }

        level.total_volume += quantity;
        level.order_count++;

        // 6. Add the new order to the order_map_.
        order_map_[order_id] = new_order;
        std::cout << "Adding order: " << order_id << std::endl;
        // TODO: Implement the logic described above.
    }

    void cancel_order(uint64_t order_id) {
        // --- LOGIC TO IMPLEMENT ---
        // 1. Find the Order* in order_map_. If not found, ignore.
        auto map_iterator = order_map_.find(order_id);
        if (map_iterator == order_map_.end()) {
            // Order not found, so there's nothing to do. Return.
            return;
        }

        // 2. From the Order*, get its price, quantity, and side.
        
        Order* this_order = map_iterator->second;

        uint32_t price = this_order->price;
        uint32_t quantity = this_order->quantity;
        bool is_bid = this_order->is_bid;

        // 3. Get the correct PriceLevel from bids_ or asks_ using the price.
        
        std::vector<PriceLevel>& side_vec = is_bid ? bids_ : asks_;
        size_t index = price - min_price_;
        PriceLevel& level = side_vec[index];
       

        // 4. Unlink the Order from the doubly-linked list.
        //    - This is the tricky part: update `order->prev->next` and `order->next->prev`.
        //    - Must handle edge cases: Is it the head? The tail? The only order?
        
        // Check if there is a node BEFORE the one we are deleting.
        if (this_order->prev != nullptr) {
            this_order->prev->next = this_order->next;
        }
        else {
            level.head = this_order->next;
        }

        // Check if there is a node AFTER the one we are deleting.
        if (this_order->next != nullptr) {
            this_order->next->prev = this_order->prev;
        }
        else {
            level.tail = this_order->prev;
        }

        // 5. Update the PriceLevel's total_volume and order_count.
        level.order_count--;
        level.total_volume -= quantity;
        // 6. Remove the order from order_map_.
        
        order_map_.erase(map_iterator);

        // 7. Use allocator_.deallocate(...) to return the memory to the pool.
        allocator_.deallocate(this_order);
        //std::cout << "Canceling order: " << order_id << std::endl;
        // TODO: Implement the logic described above.
    }

    void modify_order(uint64_t order_id, uint32_t new_quantity) {
        // --- LOGIC TO IMPLEMENT ---
        // 1. If new_quantity is 0, just call cancel_order(order_id) and return.
        
        if (new_quantity == 0) {
            cancel_order(order_id);
            return;
        }

        // 2. Find the Order* in order_map_. If not found, ignore.
         
        auto map_iterator = order_map_.find(order_id);
        if (map_iterator == order_map_.end()) {
            // Order not found, so there's nothing to do. Return.
            return;
        }
        
        Order* this_order = map_iterator->second;

        // 3. Calculate the change in quantity: `int64_t change = new_quantity - order->quantity;`
        
        int64_t change = static_cast<int64_t>(new_quantity) - this_order->quantity;
        
        // 4. Get the PriceLevel for the order.
        size_t index = this_order->price - min_price_;
        std::vector<PriceLevel>& vec = this_order->is_bid ? bids_ : asks_;
        PriceLevel& level = vec[index];

        // 5. Update the PriceLevel's total_volume by adding `change`.
        
        level.total_volume += change;
        
        // 6. Update the order's quantity: `order->quantity = new_quantity;`

        this_order->quantity = new_quantity;
        std::cout << "Modifying order: " << order_id << std::endl;
        // TODO: Implement the logic described above.
    }
    uint32_t get_best_bid() const {
        for (size_t i = bids_.size(); i > 0; --i) {
            if (bids_[i - 1].order_count > 0) {
                return min_price_ + (i - 1);
            }
        }
        return 0; // Indicates no bids
    }

    // Returns the best (lowest) ask price, or a special value if no asks exist.
    uint32_t get_best_ask() const {
        for (size_t i = 0; i < asks_.size(); ++i) {
            if (asks_[i].order_count > 0) {
                return min_price_ + i;
            }
        }
        return UINT32_MAX; // Indicates no asks
    }

    // Returns a pointer to the best bid level, or nullptr if no bids.
    // This is needed by the matching engine to get the list of orders.
    const PriceLevel* get_best_bid_level() const {
        for (size_t i = bids_.size(); i > 0; --i) {
            const auto& level = bids_[i - 1];
            if (level.order_count > 0) {
                return &level;
            }
        }
        return nullptr;
    }
    const PriceLevel* get_best_ask_level() const {
        for (size_t i = 0; i < asks_.size(); ++i) {
            const auto& level = asks_[i];
            if (level.order_count > 0) {
                return &level;
            }
        }
        return nullptr;
    }
    void print_book() const {
        std::cout << "--- ORDER BOOK ---" << std::endl;

        // Print asks in ascending price order (from best ask up)
        std::cout << "ASKS:" << std::endl;
        for (size_t i = 0; i < asks_.size(); ++i) {
            const auto& level = asks_[i] ;
            if (level.order_count > 0) {
                uint32_t price = min_price_ + i;
                std::cout << "Price: " << price << " | Volume: " << level.total_volume
                    << " | Orders: " << level.order_count << std::endl;
            }
        }

        std::cout << "------------------" << std::endl;

        // Print bids in descending price order (from best bid down)
        std::cout << "BIDS:" << std::endl;
        // We iterate backwards to get descending price
        for (size_t i = bids_.size(); i > 0; --i) {
            const auto& level = bids_[i - 1];
            if (level.order_count > 0) {
                uint32_t price = min_price_ + (i - 1);
                std::cout << "Price: " << price << " | Volume: " << level.total_volume
                    << " | Orders: " << level.order_count << std::endl;
            }
        }
        std::cout << "------------------\n" << std::endl;
    }
    uint32_t get_best_bid_price() const {
        for (size_t i = bids_.size(); i > 0; --i) {
            if (bids_[i - 1].order_count > 0) {
                return min_price_ + (i - 1);
            }
        }
        return 0; // Indicates no bids
    }

    uint32_t get_best_ask_price() const {
        for (size_t i = 0; i < asks_.size(); ++i) {
            if (asks_[i].order_count > 0) {
                return min_price_ + i;
            }
        }
        return UINT32_MAX; // Indicates no asks
    }

    PriceLevel* get_price_level(uint32_t price, bool is_bid) {
        if (price < min_price_ || price > MaxPrice) {
            return nullptr;
        }
        size_t index = price - min_price_;
        return is_bid ? &bids_[index] : &asks_[index];
    }

    // --- New helper inspectors (safe, non-pointer return values) ---
    uint64_t get_price_level_head_order_id(uint32_t price, bool is_bid) const {
        if (price < min_price_ || price > MaxPrice) return 0;
        size_t index = price - min_price_;
        const PriceLevel& level = is_bid ? bids_[index] : asks_[index];
        if (!level.head) return 0;
        return level.head->order_id;
    }

    uint32_t get_price_level_head_quantity(uint32_t price, bool is_bid) const {
        if (price < min_price_ || price > MaxPrice) return 0;
        size_t index = price - min_price_;
        const PriceLevel& level = is_bid ? bids_[index] : asks_[index];
        if (!level.head) return 0;
        return level.head->quantity;
    }

    uint32_t get_order_quantity(uint64_t order_id) const {
        auto it = order_map_.find(order_id);
        if (it == order_map_.end() || it->second == nullptr) return 0;
        return it->second->quantity;
    }
};
template<size_t MaxOrders, uint32_t MinPrice, uint32_t MaxPrice>
void matching_engine_process(OrderBook<MaxOrders, MinPrice, MaxPrice>& book,
    uint64_t order_id, uint32_t price, uint32_t quantity, bool is_bid) {

    // --- MATCHING LOGIC FOR AN INCOMING BID ---
    if (is_bid) {
        // A bid is aggressive if its price is >= the best ask price.
        // We loop as long as the bid is aggressive and still has quantity to fill.
        while (quantity > 0 && price >= book.get_best_ask_price()) {

            uint32_t best_ask_price = book.get_best_ask_price();
            const PriceLevel* ask_level = book.get_price_level(best_ask_price, false);
            Order* resting_ask = ask_level->head; // First order at the level has time priority

            uint32_t trade_quantity = std::min(quantity, resting_ask->quantity);

            // --- GENERATE A TRADE EVENT (The Output of the Engine) ---
            std::cout << ">>> TRADE! " << trade_quantity << " shares @ price " << best_ask_price << std::endl;
            std::cout << "    Incoming Bid ID: " << order_id << " matched with Resting Ask ID: " << resting_ask->order_id << std::endl;

            quantity -= trade_quantity; // Decrease the incoming order's remaining quantity

            // Update the book's state using its "dumb" methods
            if (resting_ask->quantity == trade_quantity) {
                // Resting order is fully filled
                book.cancel_order(resting_ask->order_id);
            }
            else {
                // Resting order is partially filled
                uint32_t new_quantity = resting_ask->quantity - trade_quantity;
                book.modify_order(resting_ask->order_id, new_quantity);
            }
        }

        // If any quantity remains after matching, the order becomes a new resting bid.
        if (quantity > 0) {
            book.add_order(order_id, price, quantity, is_bid);
        }

    }
    // --- MATCHING LOGIC FOR AN INCOMING ASK ---
    else {
        // An ask is aggressive if its price is <= the best bid price.
        while (quantity > 0 && price <= book.get_best_bid_price()) {
            uint32_t best_bid_price = book.get_best_bid_price();
            const PriceLevel* bid_level = book.get_price_level(best_bid_price, true);
            Order* resting_bid = bid_level->head;

            uint32_t trade_quantity = std::min(quantity, resting_bid->quantity);

            std::cout << ">>> TRADE! " << trade_quantity << " shares @ price " << best_bid_price << std::endl;
            std::cout << "    Incoming Ask ID: " << order_id << " matched with Resting Bid ID: " << resting_bid->order_id << std::endl;

            quantity -= trade_quantity;

            if (resting_bid->quantity == trade_quantity) {
                book.cancel_order(resting_bid->order_id);
            }
            else {
                uint32_t new_quantity = resting_bid->quantity - trade_quantity;
                book.modify_order(resting_bid->order_id, new_quantity);
            }
        }

        if (quantity > 0) {
            book.add_order(order_id, price, quantity, is_bid);
        }
    }
}

template<size_t MaxOrders, uint32_t MinPrice, uint32_t MaxPrice>
void process_order_file(const std::string& filename, OrderBook<MaxOrders, MinPrice, MaxPrice>& book) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file '" << filename << "'" << std::endl;
        return;
    }

    std::cout << "Successfully opened " << filename << ". Starting processing...\n" << std::endl;
    std::string line;
    int line_number = 0;

    while (std::getline(file, line)) {
        line_number++;
        std::stringstream ss(line);
        std::string action;
        ss >> action;

        if (action.empty() || action[0] == '#') {
            continue;
        }

        if (action == "ADD") {
            std::string side_str;
            uint64_t order_id;
            uint32_t price;
            uint32_t quantity;

            if (ss >> side_str >> order_id >> price >> quantity) {
                bool is_bid = (side_str == "BID");
                // ADD commands go through the smart matching engine
                matching_engine_process(book, order_id, price, quantity, is_bid);
            }
            else {
                std::cerr << "Line " << line_number << ": Malformed ADD command." << std::endl;
            }

        }
        else if (action == "CANCEL") {
            uint64_t order_id;
            if (ss >> order_id) {
                // CANCEL commands operate on resting orders, so they call the book directly
                book.cancel_order(order_id);
            }
            else {
                std::cerr << "Line " << line_number << ": Malformed CANCEL command." << std::endl;
            }

        }
        else if (action == "MODIFY") {
            uint64_t order_id;
            uint32_t new_quantity;
            if (ss >> order_id >> new_quantity) {
                // MODIFY commands also operate on resting orders, calling the book directly
                book.modify_order(order_id, new_quantity);
            }
            else {
                std::cerr << "Line " << line_number << ": Malformed MODIFY command." << std::endl;
            }

        }
        else if (action == "PRINT") {
            book.print_book();
        }
        else {
            std::cerr << "Line " << line_number << ": Unknown command '" << action << "'" << std::endl;
        }
    }

    std::cout << "\nFinished processing file." << std::endl;
}