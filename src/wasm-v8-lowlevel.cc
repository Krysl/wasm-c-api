#include "wasm-v8-lowlevel.hh"

// TODO(v8): if we don't include these, api.h does not compile
#include "objects/objects.h"
#include "objects/bigint.h"
#include "objects/managed.h"
#include "objects/managed-inl.h"
#include "objects/module.h"
#include "objects/shared-function-info.h"
#include "objects/templates.h"
#include "objects/fixed-array.h"
#include "objects/ordered-hash-table.h"
#include "objects/js-promise.h"
#include "objects/js-collection.h"

#include "api/api.h"
#include "api/api-inl.h"
#include "wasm/wasm-objects.h"
#include "wasm/wasm-objects-inl.h"
#include "wasm/wasm-serialization.h"

#include "flags/flags.h"


namespace v8 {
namespace wasm {


// Initialization

void flags_init() {
  v8::internal::v8_flags.expose_gc = true;
}


// Objects

auto object_isolate(v8::Local<v8::Object> obj) -> v8::Isolate* {
  auto v8_obj = v8::Utils::OpenHandle(*obj);
  return reinterpret_cast<v8::Isolate*>(v8_obj->GetIsolate());
}

auto object_isolate(const v8::Persistent<v8::Object>& obj) -> v8::Isolate* {
  struct FakePersistent { v8::Object* val; };
  auto v8_obj = reinterpret_cast<const FakePersistent*>(&obj)->val;
  return v8_obj->GetIsolate();
}

template<class T>
auto object_handle(v8::internal::Tagged<T> v8_obj) -> v8::internal::Handle<T> {
  return handle(v8_obj, v8_obj->GetIsolate());
}


auto object_is_module(v8::Local<v8::Object> obj) -> bool {
  auto v8_obj = v8::Utils::OpenHandle(*obj);
  return v8::internal::IsWasmModuleObject(*v8_obj);
}

auto object_is_instance(v8::Local<v8::Object> obj) -> bool {
  auto v8_obj = v8::Utils::OpenHandle(*obj);
  return IsWasmInstanceObject(*v8_obj);
}

auto object_is_func(v8::Local<v8::Object> obj) -> bool {
  auto v8_obj = v8::Utils::OpenHandle(*obj);
  return v8::internal::WasmExportedFunction::IsWasmExportedFunction(*v8_obj);
}

auto object_is_global(v8::Local<v8::Object> obj) -> bool {
  auto v8_obj = v8::Utils::OpenHandle(*obj);
  return IsWasmGlobalObject(*v8_obj);
}

auto object_is_table(v8::Local<v8::Object> obj) -> bool {
  auto v8_obj = v8::Utils::OpenHandle(*obj);
  return IsWasmTableObject(*v8_obj);
}

auto object_is_memory(v8::Local<v8::Object> obj) -> bool {
  auto v8_obj = v8::Utils::OpenHandle(*obj);
  return v8::internal::IsWasmMemoryObject(*v8_obj);
}

auto object_is_error(v8::Local<v8::Object> obj) -> bool {
  auto v8_obj = v8::Utils::OpenHandle(*obj);
  return IsJSError(*v8_obj);
}



// Foreign pointers

auto foreign_new(v8::Isolate* isolate, void* ptr) -> v8::Local<v8::Value> {
  auto foreign = v8::FromCData(
    reinterpret_cast<v8::internal::Isolate*>(isolate),
    reinterpret_cast<v8::internal::Address>(ptr)
  );
  return v8::Utils::ToLocal(foreign);
}

auto foreign_get(v8::Local<v8::Value> val) -> void* {
  auto foreign = v8::Utils::OpenHandle(*val);
  if (!IsForeign(*foreign)) return nullptr;
  auto addr = v8::ToCData<v8::internal::Address>(*foreign);
  return reinterpret_cast<void*>(addr);
}


struct ManagedData {
  static constexpr i::ExternalPointerTag kManagedTag = i::kWasmManagedDataTag;
  ManagedData(void* info, void (*finalizer)(void*)) :
    info(info), finalizer(finalizer) {}

  ~ManagedData() {
    if (finalizer) (*finalizer)(info);
  }

  void* info;
  void (*finalizer)(void*);
};


auto managed_new(v8::Isolate* isolate, void* ptr, void (*finalizer)(void*)) -> v8::Local<v8::Value> {
  auto managed = v8::internal::Managed<ManagedData>::FromUniquePtr(
    reinterpret_cast<v8::internal::Isolate*>(isolate), sizeof(ManagedData),
    std::unique_ptr<ManagedData>(new ManagedData(ptr, finalizer))
  );
  return v8::Utils::ToLocal(managed);
}

auto managed_get(v8::Local<v8::Value> val) -> void* {
  auto v8_val = v8::Utils::OpenHandle(*val);
  if (!IsForeign(*v8_val)) return nullptr;
  auto managed =
    v8::internal::Handle<v8::internal::Managed<ManagedData>>::cast(v8_val);
  return managed->raw()->info;
}


// Types

auto v8_valtype_to_wasm(v8::internal::wasm::ValueType v8_valtype) -> val_kind_t {
  switch (v8_valtype.kind()) {
    case v8::internal::wasm::kI32: return I32;
    case v8::internal::wasm::kI64: return I64;
    case v8::internal::wasm::kF32: return F32;
    case v8::internal::wasm::kF64: return F64;
    case v8::internal::wasm::kRefNull:
      switch (v8_valtype.heap_type().representation()) {
        case v8::internal::wasm::HeapType::Representation::kFunc:
          return FUNCREF;
        case v8::internal::wasm::HeapType::Representation::kExtern:
          return EXTERNREF;
        default: ;
      }
      break;
    default: ;
  }
  UNREACHABLE();
}

auto func_type_param_arity(v8::Local<v8::Object> function) -> uint32_t {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(function);
  auto v8_function = v8::internal::Handle<v8::internal::WasmExportedFunction>::cast(v8_object);
  auto sig =
    v8_function->instance()->module()->functions[v8_function->function_index()].sig;
  return static_cast<uint32_t>(sig->parameter_count());
}

auto func_type_result_arity(v8::Local<v8::Object> function) -> uint32_t {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(function);
  auto v8_function = v8::internal::Handle<v8::internal::WasmExportedFunction>::cast(v8_object);
  auto sig =
    v8_function->instance()->module()->functions[v8_function->function_index()].sig;
  return static_cast<uint32_t>(sig->return_count());
}

auto func_type_param(v8::Local<v8::Object> function, size_t i) -> val_kind_t {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(function);
  auto v8_function = v8::internal::Handle<v8::internal::WasmExportedFunction>::cast(v8_object);
  auto sig =
    v8_function->instance()->module()->functions[v8_function->function_index()].sig;
  return v8_valtype_to_wasm(sig->GetParam(i));
}

auto func_type_result(v8::Local<v8::Object> function, size_t i) -> val_kind_t {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(function);
  auto v8_function = v8::internal::Handle<v8::internal::WasmExportedFunction>::cast(v8_object);
  auto sig =
    v8_function->instance()->module()->functions[v8_function->function_index()].sig;
  return v8_valtype_to_wasm(sig->GetReturn(i));
}

auto global_type_content(v8::Local<v8::Object> global) -> val_kind_t {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(global);
  auto v8_global = v8::internal::Handle<v8::internal::WasmGlobalObject>::cast(v8_object);
  return v8_valtype_to_wasm(v8_global->type());
}

auto global_type_mutable(v8::Local<v8::Object> global) -> bool {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(global);
  auto v8_global = v8::internal::Handle<v8::internal::WasmGlobalObject>::cast(v8_object);
  return v8_global->is_mutable();
}

auto table_type_min(v8::Local<v8::Object> table) -> uint32_t {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(table);
  auto v8_table = v8::internal::Handle<v8::internal::WasmTableObject>::cast(v8_object);
  return v8_table->current_length();
}

auto table_type_max(v8::Local<v8::Object> table) -> uint32_t {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(table);
  auto v8_table = v8::internal::Handle<v8::internal::WasmTableObject>::cast(v8_object);
  auto v8_max_obj = v8_table->maximum_length();
  uint32_t max;
  return v8::internal::Object::ToUint32(v8_max_obj, &max) ? max : 0xffffffffu;
}

auto memory_type_min(v8::Local<v8::Object> memory) -> uint32_t {
  return memory_size(memory);
}

auto memory_type_max(v8::Local<v8::Object> memory) -> uint32_t {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(memory);
  auto v8_memory = v8::internal::Handle<v8::internal::WasmMemoryObject>::cast(v8_object);
  return v8_memory->has_maximum_pages() ? v8_memory->maximum_pages() : 0xffffffffu;
}


// Modules

auto module_binary_size(v8::Local<v8::Object> module) -> size_t {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(module);
  auto v8_module = v8::internal::Handle<v8::internal::WasmModuleObject>::cast(v8_object);
  return v8_module->native_module()->wire_bytes().size();
}

auto module_binary(v8::Local<v8::Object> module) -> const char* {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(module);
  auto v8_module = v8::internal::Handle<v8::internal::WasmModuleObject>::cast(v8_object);
  return reinterpret_cast<const char*>(v8_module->native_module()->wire_bytes().begin());
}

void module_compile(v8::Local<v8::Object> module) {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(module);
  auto v8_module = v8::internal::Handle<v8::internal::WasmModuleObject>::cast(v8_object);
  v8_module->native_module()->compilation_state()->TierUpAllFunctions();
}

auto module_serialize_size(v8::Local<v8::Object> module) -> size_t {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(module);
  auto v8_module = v8::internal::Handle<v8::internal::WasmModuleObject>::cast(v8_object);
  v8::internal::wasm::WasmSerializer serializer(v8_module->native_module());
  return serializer.GetSerializedNativeModuleSize();
}

auto module_serialize(v8::Local<v8::Object> module, char* buffer, size_t size) -> bool {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(module);
  auto v8_module = v8::internal::Handle<v8::internal::WasmModuleObject>::cast(v8_object);
  v8::internal::wasm::WasmSerializer serializer(v8_module->native_module());
  return serializer.SerializeNativeModule({reinterpret_cast<uint8_t*>(buffer), size});
}

auto module_deserialize(
  v8::Isolate* isolate,
  const uint8_t* binary, size_t binary_size,
  const uint8_t* buffer, size_t buffer_size
) -> v8::MaybeLocal<v8::Object> {
  auto v8_isolate = reinterpret_cast<v8::internal::Isolate*>(isolate);
  auto maybe_v8_module =
    v8::internal::wasm::DeserializeNativeModule(v8_isolate,
      {buffer, buffer_size},
      {binary, binary_size},
      {},
      {"", 0});
  if (maybe_v8_module.is_null()) return v8::MaybeLocal<v8::Object>();
  auto v8_module = v8::internal::Handle<v8::internal::JSObject>::cast(maybe_v8_module.ToHandleChecked());
  return v8::MaybeLocal<v8::Object>(v8::Utils::ToLocal(v8_module));
}


// Instances

auto instance_module(v8::Local<v8::Object> instance) -> v8::Local<v8::Object> {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(instance);
  auto v8_instance = v8::internal::Handle<v8::internal::WasmInstanceObject>::cast(v8_object);
  auto v8_module = object_handle(v8::internal::JSObject::cast(v8_instance->module_object()));
  return v8::Utils::ToLocal(v8_module);
}

auto instance_exports(v8::Local<v8::Object> instance) -> v8::Local<v8::Object> {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(instance);
  auto v8_instance = v8::internal::Handle<v8::internal::WasmInstanceObject>::cast(v8_object);
  auto v8_exports = object_handle(v8_instance->exports_object());
  return v8::Utils::ToLocal(v8_exports);
}


// Externals

auto extern_kind(v8::Local<v8::Object> external) -> extern_kind_t {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(external);

  if (v8::internal::WasmExportedFunction::IsWasmExportedFunction(*v8_object)) return EXTERN_FUNC;
  if (v8::internal::IsWasmGlobalObject(*v8_object)) return EXTERN_GLOBAL;
  if (v8::internal::IsWasmTableObject(*v8_object)) return EXTERN_TABLE;
  if (v8::internal::IsWasmMemoryObject(*v8_object)) return EXTERN_MEMORY;
  UNREACHABLE();
}


// Functions

auto func_instance(v8::Local<v8::Function> function) -> v8::Local<v8::Object> {
  auto v8_function = v8::Utils::OpenHandle(*function);
  auto v8_func = v8::internal::Handle<v8::internal::WasmExportedFunction>::cast(v8_function);
  auto v8_instance = object_handle(v8::internal::JSObject::cast(v8_func->instance()));
  return v8::Utils::ToLocal(v8_instance);
}


// Globals

auto global_get_i32(v8::Local<v8::Object> global) -> int32_t {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(global);
  auto v8_global = v8::internal::Handle<v8::internal::WasmGlobalObject>::cast(v8_object);
  return v8_global->GetI32();
}
auto global_get_i64(v8::Local<v8::Object> global) -> int64_t {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(global);
  auto v8_global = v8::internal::Handle<v8::internal::WasmGlobalObject>::cast(v8_object);
  return v8_global->GetI64();
}
auto global_get_f32(v8::Local<v8::Object> global) -> float {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(global);
  auto v8_global = v8::internal::Handle<v8::internal::WasmGlobalObject>::cast(v8_object);
  return v8_global->GetF32();
}
auto global_get_f64(v8::Local<v8::Object> global) -> double {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(global);
  auto v8_global = v8::internal::Handle<v8::internal::WasmGlobalObject>::cast(v8_object);
  return v8_global->GetF64();
}
auto global_get_ref(v8::Local<v8::Object> global) -> v8::Local<v8::Value> {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(global);
  auto v8_global = v8::internal::Handle<v8::internal::WasmGlobalObject>::cast(v8_object);
  return v8::Utils::ToLocal(v8_global->GetRef());
}

void global_set_i32(v8::Local<v8::Object> global, int32_t val) {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(global);
  auto v8_global = v8::internal::Handle<v8::internal::WasmGlobalObject>::cast(v8_object);
  v8_global->SetI32(val);
}
void global_set_i64(v8::Local<v8::Object> global, int64_t val) {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(global);
  auto v8_global = v8::internal::Handle<v8::internal::WasmGlobalObject>::cast(v8_object);
  v8_global->SetI64(val);
}
void global_set_f32(v8::Local<v8::Object> global, float val) {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(global);
  auto v8_global = v8::internal::Handle<v8::internal::WasmGlobalObject>::cast(v8_object);
  v8_global->SetF32(val);
}
void global_set_f64(v8::Local<v8::Object> global, double val) {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(global);
  auto v8_global = v8::internal::Handle<v8::internal::WasmGlobalObject>::cast(v8_object);
  v8_global->SetF64(val);
}
void global_set_ref(v8::Local<v8::Object> global, v8::Local<v8::Value> val) {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(global);
  auto v8_global = v8::internal::Handle<v8::internal::WasmGlobalObject>::cast(v8_object);
  v8_global->SetRef(v8::Utils::OpenHandle<v8::Value, v8::internal::Object>(val));
}


// Tables

auto table_get(v8::Local<v8::Object> table, size_t index) -> v8::MaybeLocal<v8::Value> {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(table);
  auto v8_table = v8::internal::Handle<v8::internal::WasmTableObject>::cast(v8_object);
  // TODO(v8): This should happen in WasmTableObject::Get.
  if (index >= size_t(v8_table->current_length()))
    return v8::MaybeLocal<v8::Value>();

  i::Isolate* isolate = v8_table->GetIsolate();
  v8::internal::Handle<v8::internal::Object> v8_value =
    v8::internal::WasmTableObject::Get(
      v8_table->GetIsolate(), v8_table, static_cast<uint32_t>(index));

  if (v8::internal::IsWasmFuncRef(*v8_value)) {
    v8_value = i::WasmInternalFunction::GetOrCreateExternal(
        i::handle(i::WasmFuncRef::cast(*v8_value)->internal(isolate), isolate));
  } else if (v8::internal::IsWasmNull(*v8_value) || v8::internal::IsNull(*v8_value)) {
    return Local<v8::Value>();
  }

  return v8::Utils::ToLocal(v8::internal::Handle<v8::internal::Object>::cast(v8_value));
}

auto table_set(
  v8::Local<v8::Object> table, size_t index, v8::Local<v8::Value> value
) -> bool {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(table);
  auto v8_table = v8::internal::Handle<v8::internal::WasmTableObject>::cast(v8_object);
  auto v8_value = v8::Utils::OpenHandle<v8::Value, v8::internal::Object>(value);
  // TODO(v8): This should happen in WasmTableObject::Set.
  if (index >= size_t(v8_table->current_length())) return false;
  auto isolate = v8_table->GetIsolate();
  const char *error_message;
  v8_value = i::wasm::JSToWasmObject(isolate, nullptr, v8_value,
                                     v8_table->type(), &error_message)
                 .ToHandleChecked();
  { v8::TryCatch handler(table->GetIsolate());
    v8::internal::WasmTableObject::Set(isolate, v8_table,
      static_cast<uint32_t>(index), v8_value);
    if (handler.HasCaught()) return false;
  }

  return true;
}

auto table_size(v8::Local<v8::Object> table) -> size_t {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(table);
  auto v8_table = v8::internal::Handle<v8::internal::WasmTableObject>::cast(v8_object);
  return v8_table->current_length();
}

auto table_grow(
  v8::Local<v8::Object> table, size_t delta, v8::Local<v8::Value> init
) -> bool {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(table);
  auto v8_table = v8::internal::Handle<v8::internal::WasmTableObject>::cast(v8_object);
  if (delta > 0xfffffffflu) return false;
  auto old_size = v8_table->current_length();
  auto new_size = old_size + static_cast<uint32_t>(delta);
  // TODO(v8): This should happen in WasmTableObject::Grow.
  if (new_size > table_type_max(table)) return false;

  auto isolate = v8_table->GetIsolate();
  const char* error_message;
  auto handle_object = v8::Utils::OpenHandle<v8::Value, v8::internal::Object>(init);
  auto wasm_object =
      i::wasm::JSToWasmObject(isolate, nullptr, handle_object, v8_table->type(),
                              &error_message)
          .ToHandleChecked();
  { v8::TryCatch handler(table->GetIsolate());
    v8::internal::WasmTableObject::Grow(
      v8_table->GetIsolate(), v8_table, static_cast<uint32_t>(delta),
      wasm_object);
    if (handler.HasCaught()) return false;
  }

  return true;
}


// Memory

auto memory_data(v8::Local<v8::Object> memory) -> char* {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(memory);
  auto v8_memory = v8::internal::Handle<v8::internal::WasmMemoryObject>::cast(v8_object);
  return reinterpret_cast<char*>(v8_memory->array_buffer()->backing_store());
}

auto memory_data_size(v8::Local<v8::Object> memory)-> size_t {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(memory);
  auto v8_memory = v8::internal::Handle<v8::internal::WasmMemoryObject>::cast(v8_object);
  return v8_memory->array_buffer()->byte_length();
}

auto memory_size(v8::Local<v8::Object> memory) -> uint32_t {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(memory);
  auto v8_memory = v8::internal::Handle<v8::internal::WasmMemoryObject>::cast(v8_object);
  return static_cast<uint32_t>(
    v8_memory->array_buffer()->byte_length() / v8::internal::wasm::kWasmPageSize);
}

auto memory_grow(v8::Local<v8::Object> memory, uint32_t delta) -> bool {
  auto v8_object = v8::Utils::OpenHandle<v8::Object, v8::internal::JSReceiver>(memory);
  auto v8_memory = v8::internal::Handle<v8::internal::WasmMemoryObject>::cast(v8_object);
  auto old = v8::internal::WasmMemoryObject::Grow(
    v8_memory->GetIsolate(), v8_memory, delta);
  return old != -1;
}

}  // namespace wasm

template class internal::Managed<wasm::ManagedData>;

}  // namespace v8
