#pragma once

#include "kos_app.h"

namespace kos {

class App;

namespace registry {

// 由 kos.cpp::boot() 调用,把 build artifacts 中所有 KOS_APP_DEFINE 注册的
// App 实例登记到内部表里。
void init();

// 已注册 App 的总数。
int count();

// 返回第 i 个注册 App 的引用。索引由链接顺序决定。
App &app(int i);

// 找到指定 id 的 App。找不到返回 nullptr。
App *find(const char *id);

// 启动 id 对应的 App,挂起当前 App。
// 调用者必须持有 LVGL port lock。
void launch(const char *id);

// 当前活动 App。
App *current();

}  // namespace registry

namespace registry {
namespace internals {

struct Entry {
    const char *id;
    App *instance;
};

class StaticRegistrar {
public:
    StaticRegistrar(const Entry &e);
};

// 通用 add 接口 —— KOS_APP_DEFINE 通过 StaticRegistrar 调用。
void add_entry(const Entry &e);

}  // namespace internals
}  // namespace registry

// paste2 / KOS_APP_DEFINE(id_str, Klass)
//
// 用法示例:
//   class AppPet : public kos::App { ... };
//   KOS_APP_DEFINE("pet", AppPet);
//
// 宏需要做几件事:
//   1. 声明并定义一个静态对象 Klass,Klass 可能是限定名(kos::AppLauncher)
//   2. 注册该对象的地址到注册表
//
// 用 __LINE__ 作为唯一后缀避免重名。
//
// GCC 处理 ##__LINE__ 时会把 __LINE__ 当作 bare identifier 而不是先扩展成数字。
// 我们通过将 __LINE__ 替换为二次 paste(BOOST_PP style)解决:
//
//   PASTE_LINENO(l)  -> "_lineno_<LINE>"
//   COSO_CONCAT(a,b) -> a##b (delay evaluation one step)
//
// 但 GNU strict paste 仍然不让 — 所以改用更直接的设计:让 macro call site 传一个
// `INST` token 作为唯一标识。失败 → 改让宏修改 s_entries 改为构造时 push 实例指针。
#define COSO_CONCAT_(a, b) a##b
#define COSO_CONCAT(a, b)  COSO_CONCAT_(a, b)

// 注册表 helper:直接把实例指针和 id 推入(避免 token-paste 复杂性)。
class AutoRegisterForInstance {
public:
    AutoRegisterForInstance(const char *id, kos::App *instance);
};

// KOS_APP_DEFINE(id_str, Klass)
//
// 用户用法:(Klass 限定名 OK,因为我们在命名空间内展开宏)
//   KOS_APP_DEFINE("launcher", AppLauncher);
//
// 内部展开 —— 用 auto-registrar 模板 + 静态对象:
//
//   static Klass _kos_app_inst_<unique>;   // 静态实例,storage 全局
//   static Registerer<Klass> _kos_app_reg_<unique>(id_str, &_kos_app_inst_<unique>);
//
// `unique` 由调用方传 —— 我们让用户传(GCC ## token 的可靠性无法保证,改用模板)。
// 或者用文件级 / 类级 namespace 配对方案 —— 二者都太复杂,直接走模板+函数级 lambdas.
//
// 实际实现:宏只生成 Klass 的静态实例,注册机制通过 Klass 自身构造函数体调用
// AutoRegisterForInstance::ctor 来完成 —— 这样没有 ## paste 问题。
//
// 由于 Klass 自定义构造可能与 AutoRegisterForInstance 无关,我们改用
// 静态全局构造 vs 类级构造函数:
//   static Klass _kos_app_inst_<unique>;
//   struct _kos_app_entry_<unique> {
//       _kos_app_entry_<unique>() {
//           ::kos::registry::internals::register_app(id_str,
//                                                     &_kos_app_inst_<unique>,
//                                                     Klass::manifest_static);
//       }
//   } _kos_app_regar_<unique>;
//
// <unique> 用 file_static 内 __LINE__ 改成 caller-supplied `Inst` 参数。
#define KOS_APP_DEFINE(id_str, Klass, Inst)                                          \
    static Klass _kos_inst_##Inst;                                                   \
    static const ::kos::registry::internals::Entry _kos_entry_##Inst = {             \
        id_str, static_cast<::kos::App *>(&_kos_inst_##Inst),                        \
    };                                                                               \
    static ::kos::registry::internals::StaticRegistrar                               \
        _kos_regar_##Inst(_kos_entry_##Inst);

}  // namespace kos
