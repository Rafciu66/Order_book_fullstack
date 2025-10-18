from setuptools import setup, Extension
import pybind11

# The name of the module must match the one in PYBIND11_MODULE
module_name = "my_engine"

ext_modules = [
    Extension(
        module_name,
        ["bindings.cpp"], # List of your C++ source files
        include_dirs=[
            pybind11.get_include(),
        ],
        language='c++',
        extra_compile_args=["/std:c++17", "/O2"], # Use C++17 and optimize for speed
    ),
]

setup(
    name=module_name,
    version="0.1.0",
    author="Your Name",
    description="HFT Order Book in C++",
    ext_modules=ext_modules,
)