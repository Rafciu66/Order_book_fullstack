import my_engine # Import your freshly compiled C++ module!

print("Python backend is starting up...")

# 1. You can now create an instance of your C++ OrderBook class in Python!
book = my_engine.OrderBook()

print("Created C++ OrderBook object in Python.")

# Let's simulate the trades.txt file processing directly in Python
print("\n--- Setting up initial book state ---")
my_engine.matching_engine_process(book, 101, 100, 50, True)  # ADD BID
my_engine.matching_engine_process(book, 102, 100, 25, True)  # ADD BID
my_engine.matching_engine_process(book, 201, 101, 40, False) # ADD ASK
my_engine.matching_engine_process(book, 202, 101, 30, False) # ADD ASK
my_engine.matching_engine_process(book, 203, 102, 100, False) # ADD ASK

book.print_book() # This will print from C++ to your console

print("\n--- Processing an aggressive BID order ---")
# This bid for 80 @ 101 should trigger trades
my_engine.matching_engine_process(book, 103, 101, 80, True) # ADD aggressive BID

book.print_book()

print("\n--- Querying book state from Python ---")
best_bid = book.get_best_bid_price()
best_ask = book.get_best_ask_price()

print(f"Current best bid from Python: {best_bid}")
print(f"Current best ask from Python: {best_ask}")

print("\nPython backend finished.")