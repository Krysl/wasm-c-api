#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>
#include <cinttypes>

#include "wasm.hh"

// A function to be called from Wasm code.
auto fail_callback(const wasm::vec<wasm::Val>& args) -> wasm::Result {
  std::cout << "Calling back..." << std::endl;
  return wasm::Result(wasm::Name::make(std::string("callback abort")));
}


void run(int argc, const char* argv[]) {
  // Initialize.
  std::cout << "Initializing..." << std::endl;
  auto engine = wasm::Engine::make(argc, argv);
  auto store = wasm::Store::make(engine);

  // Load binary.
  std::cout << "Loading binary..." << std::endl;
  std::ifstream file("trap.wasm");
  file.seekg(0, std::ios_base::end);
  auto file_size = file.tellg();
  file.seekg(0);
  auto binary = wasm::vec<byte_t>::make_uninitialized(file_size);
  file.read(binary.get(), file_size);
  file.close();
  if (file.fail()) {
    std::cout << "> Error loading module!" << std::endl;
    return;
  }

  // Compile.
  std::cout << "Compiling module..." << std::endl;
  auto module = wasm::Module::make(store, binary);
  if (!module) {
    std::cout << "> Error compiling module!" << std::endl;
    return;
  }

  // Create external print functions.
  std::cout << "Creating callback..." << std::endl;
  auto fail_type = wasm::FuncType::make(
    wasm::vec<wasm::ValType*>::make(),
    wasm::vec<wasm::ValType*>::make(wasm::ValType::make(wasm::I32))
  );
  auto fail_func = wasm::Func::make(store, fail_type, fail_callback);

  // Instantiate.
  std::cout << "Instantiating module..." << std::endl;
  auto imports = wasm::vec<wasm::Extern*>::make(fail_func);
  auto instance = wasm::Instance::make(store, module, imports);
  if (!instance) {
    std::cout << "> Error instantiating module!" << std::endl;
    return;
  }

  // Extract export.
  std::cout << "Extracting exports..." << std::endl;
  auto exports = instance->exports();
  if (exports.size() < 2 ||
      exports[0]->kind() != wasm::EXTERN_FUNC || !exports[0]->func() ||
      exports[1]->kind() != wasm::EXTERN_FUNC || !exports[1]->func()) {
    std::cout << "> Error accessing exports!" << std::endl;
    return;
  }

  // Call.
  for (size_t i = 0; i < 2; ++i) {
    std::cout << "Calling export " << i << "..." << std::endl;
    auto result = exports[i]->func()->call();
    if (result.kind() != wasm::Result::TRAP) {
      std::cout << "> Error calling function!" << std::endl;
      return;
    }

    std::cout << "Printing message..." << std::endl;
    std::cout << "> " << result.trap().get() << std::endl;
  }

  // Shut down.
  std::cout << "Shutting down..." << std::endl;
}


int main(int argc, const char* argv[]) {
  run(argc, argv);
  std::cout << "Done." << std::endl;
  return 0;
}
