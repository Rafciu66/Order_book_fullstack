#include <pybind11/pybind11.h>
#include "orderbook.h" // Include your header

namespace py = pybind11;

// Since our OrderBook is a template, we need to create a specific, concrete
// type that we can expose to Python. Let's call it PyOrderBook.
using PyOrderBook = OrderBook<1000, 0, 1000>;

// The PYBIND11_MODULE macro creates the entry point that Python will call
// when you `import my_engine`. The module name here ('my_engine') MUST
// match the name we will use in our build script.
PYBIND11_MODULE(my_engine, m) {
    m.doc() = "High-performance C++ Order Book and Matching Engine"; // Optional module docstring

    // Expose the OrderBook class to Python
    py::class_<PyOrderBook>(m, "OrderBook")
        // Expose the constructor. py::init creates a constructor.
        .def(py::init<>())

        // Expose public methods. The first argument is the Python name,
        // the second is a pointer to the C++ member function.
        .def("add_order", &PyOrderBook::add_order, "Adds a new resting order to the book")
        .def("cancel_order", &PyOrderBook::cancel_order, "Cancels an existing order")
        .def("modify_order", &PyOrderBook::modify_order, "Modifies an existing order")
        
        // Expose the "getter" methods so Python can query the book state
        .def("get_best_bid_price", &PyOrderBook::get_best_bid_price)
        .def("get_best_ask_price", &PyOrderBook::get_best_ask_price)
        // Keep get_price_level if you want, but expose safe inspectors:
        .def("get_price_level_head_order_id", &PyOrderBook::get_price_level_head_order_id,
             py::arg("price"), py::arg("is_bid") = true,
             "Return head order_id at price level (0 if none)")
        .def("get_price_level_head_quantity", &PyOrderBook::get_price_level_head_quantity,
             py::arg("price"), py::arg("is_bid") = true,
             "Return head order quantity at price level (0 if none)")
        .def("get_order_quantity", &PyOrderBook::get_order_quantity,
             py::arg("order_id"),
             "Return current quantity for given order id (0 if not found)")
        .def("print_book", &PyOrderBook::print_book);

    // Now, expose the standalone matching_engine_process function.
    // The syntax is m.def("python_name", &cpp_function_name, "docstring");
    m.def("matching_engine_process",
          &matching_engine_process<1000, 0, 1000>,
          "Processes an incoming order, matching if possible, otherwise adding to the book");
}