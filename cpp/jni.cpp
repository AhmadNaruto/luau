#include <jni.h>
#include <lua.h>
#include <lualib.h>
#include <luacode.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <android/log.h>

#define LOG_TAG "LuauJNI"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static JavaVM* g_jvm = nullptr;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;
    return JNI_VERSION_1_6;
}

static std::string jstring_to_string(JNIEnv* env, jstring jstr) {
    if (!jstr) return "";
    const char* chars = env->GetStringUTFChars(jstr, nullptr);
    std::string str(chars);
    env->ReleaseStringUTFChars(jstr, chars);
    return str;
}

// Map lua_State* -> callbacks for native functions
static std::mutex g_callbacks_mutex;
static std::unordered_map<lua_State*, std::unordered_map<std::string, jobject>> g_vm_callbacks;

static jobject luau_value_to_jobject(JNIEnv* env, lua_State* L, int idx, jobject thiz);

static int host_function_dispatcher(lua_State* L) {
    std::string func_name = "";
    if (lua_isstring(L, lua_upvalueindex(1))) {
        func_name = lua_tostring(L, lua_upvalueindex(1));
    }

    jobject callback_obj = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_callbacks_mutex);
        auto it = g_vm_callbacks.find(L);
        if (it != g_vm_callbacks.end()) {
            auto cb_it = it->second.find(func_name);
            if (cb_it != it->second.end()) {
                callback_obj = cb_it->second;
            }
        }
    }

    if (!callback_obj) {
        luaL_error(L, "Callback '%s' not registered in Host", func_name.c_str());
        return 0;
    }

    JNIEnv* env = nullptr;
    if (g_jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        luaL_error(L, "Failed to get JNIEnv in callback dispatcher");
        return 0;
    }

    int top = lua_gettop(L);
    jclass list_cls = env->FindClass("java/util/ArrayList");
    jmethodID list_init = env->GetMethodID(list_cls, "<init>", "(I)V");
    jmethodID list_add = env->GetMethodID(list_cls, "add", "(Ljava/lang/Object;)Z");

    jobject args_list = env->NewObject(list_cls, list_init, top);

    for (int i = 1; i <= top; ++i) {
        jobject arg_val = luau_value_to_jobject(env, L, i, nullptr);
        env->CallBooleanMethod(args_list, list_add, arg_val);
        env->DeleteLocalRef(arg_val);
    }

    jclass callback_cls = env->GetObjectClass(callback_obj);
    jmethodID invoke_mid = env->GetMethodID(callback_cls, "invoke", "(Ljava/util/List;)Lio/github/luau_jni/LuauValue;");

    jobject res_val = env->CallObjectMethod(callback_obj, invoke_mid, args_list);

    env->DeleteLocalRef(args_list);
    env->DeleteLocalRef(list_cls);
    env->DeleteLocalRef(callback_cls);

    if (!res_val) {
        lua_pushnil(L);
        return 1;
    }

    // Push LuauValue returned from Kotlin callback onto Lua stack
    jclass luau_val_cls = env->GetObjectClass(res_val);
    jfieldID type_fid = env->GetFieldID(luau_val_cls, "type", "Lio/github/luau_jni/LuauType;");
    jobject type_obj = env->GetObjectField(res_val, type_fid);

    jclass luau_type_cls = env->GetObjectClass(type_obj);
    jfieldID val_fid = env->GetFieldID(luau_type_cls, "value", "I");
    jint type_enum_val = env->GetIntField(type_obj, val_fid);

    env->DeleteLocalRef(luau_type_cls);
    env->DeleteLocalRef(type_obj);

    switch (type_enum_val) {
        case 0: // NIL
            lua_pushnil(L);
            break;
        case 1: { // BOOLEAN
            jmethodID as_bool = env->GetMethodID(luau_val_cls, "asBoolean", "(Z)Z");
            jboolean b = env->CallBooleanMethod(res_val, as_bool, false);
            lua_pushboolean(L, b);
            break;
        }
        case 3: { // NUMBER
            jmethodID as_dbl = env->GetMethodID(luau_val_cls, "asDouble", "(D)D");
            jdouble d = env->CallDoubleMethod(res_val, as_dbl, 0.0);
            lua_pushnumber(L, d);
            break;
        }
        case 4: { // STRING
            jmethodID as_str = env->GetMethodID(luau_val_cls, "asString", "(Ljava/lang/String;)Ljava/lang/String;");
            jstring js = (jstring)env->CallObjectMethod(res_val, as_str, nullptr);
            std::string s = jstring_to_string(env, js);
            if (js) env->DeleteLocalRef(js);
            lua_pushstring(L, s.c_str());
            break;
        }
        default:
            lua_pushnil(L);
            break;
    }

    env->DeleteLocalRef(luau_val_cls);
    env->DeleteLocalRef(res_val);

    return 1;
}

static jobject luau_value_to_jobject(JNIEnv* env, lua_State* L, int idx, jobject thiz) {
    jclass luau_val_cls = env->FindClass("io/github/luau_jni/LuauValue");

    int type = lua_type(L, idx);
    switch (type) {
        case LUA_TNIL: {
            jfieldID nil_fid = env->GetStaticFieldID(luau_val_cls, "NIL", "Lio/github/luau_jni/LuauValue;");
            return env->GetStaticObjectField(luau_val_cls, nil_fid);
        }
        case LUA_TBOOLEAN: {
            jmethodID value_of = env->GetStaticMethodID(luau_val_cls, "valueOf", "(Z)Lio/github/luau_jni/LuauValue;");
            return env->CallStaticObjectMethod(luau_val_cls, value_of, (jboolean)lua_toboolean(L, idx));
        }
        case LUA_TNUMBER: {
            jmethodID value_of = env->GetStaticMethodID(luau_val_cls, "valueOf", "(D)Lio/github/luau_jni/LuauValue;");
            return env->CallStaticObjectMethod(luau_val_cls, value_of, (jdouble)lua_tonumber(L, idx));
        }
        case LUA_TSTRING: {
            jmethodID value_of = env->GetStaticMethodID(luau_val_cls, "valueOf", "(Ljava/lang/String;)Lio/github/luau_jni/LuauValue;");
            jstring js = env->NewStringUTF(lua_tostring(L, idx));
            jobject res = env->CallStaticObjectMethod(luau_val_cls, value_of, js);
            env->DeleteLocalRef(js);
            return res;
        }
        case LUA_TTABLE:
        case LUA_TFUNCTION: {
            lua_pushvalue(L, idx);
            int ref = lua_ref(L, -1);
            lua_pop(L, 1);

            jmethodID ref_mid = env->GetStaticMethodID(luau_val_cls, "ref", "(Lio/github/luau_jni/LuauType;JLio/github/luau_jni/LuauVM;)Lio/github/luau_jni/LuauValue;");

            jclass type_enum_cls = env->FindClass("io/github/luau_jni/LuauType");
            jmethodID from_int = env->GetStaticMethodID(type_enum_cls, "fromInt", "(I)Lio/github/luau_jni/LuauType;");
            int type_code = (type == LUA_TTABLE) ? 5 : 6;
            jobject type_enum = env->CallStaticObjectMethod(type_enum_cls, from_int, type_code);

            jobject res = env->CallStaticObjectMethod(luau_val_cls, ref_mid, type_enum, (jlong)ref, thiz);
            env->DeleteLocalRef(type_enum_cls);
            env->DeleteLocalRef(type_enum);
            return res;
        }
        default: {
            jfieldID nil_fid = env->GetStaticFieldID(luau_val_cls, "NIL", "Lio/github/luau_jni/LuauValue;");
            return env->GetStaticObjectField(luau_val_cls, nil_fid);
        }
    }
}

extern "C" JNIEXPORT jlong JNICALL
Java_io_github_luau_1jni_LuauVM_nativeCreate(JNIEnv* env, jobject thiz) {
    lua_State* L = luaL_newstate();
    if (L) {
        luaL_openlibs(L);

        // Clear dangerous sandbox functions
        lua_pushnil(L); lua_setglobal(L, "luajava");
        lua_pushnil(L); lua_setglobal(L, "io");
        lua_pushnil(L); lua_setglobal(L, "load");
        lua_pushnil(L); lua_setglobal(L, "loadfile");
        lua_pushnil(L); lua_setglobal(L, "loadstring");
        lua_pushnil(L); lua_setglobal(L, "dofile");
        lua_pushnil(L); lua_setglobal(L, "require");
        lua_pushnil(L); lua_setglobal(L, "package");
        lua_pushnil(L); lua_setglobal(L, "debug");
    }
    return reinterpret_cast<jlong>(L);
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_luau_1jni_LuauVM_nativeDestroy(JNIEnv* env, jobject thiz, jlong handle) {
    lua_State* L = reinterpret_cast<lua_State*>(handle);
    if (L) {
        {
            std::lock_guard<std::mutex> lock(g_callbacks_mutex);
            auto it = g_vm_callbacks.find(L);
            if (it != g_vm_callbacks.end()) {
                for (auto& pair : it->second) {
                    env->DeleteGlobalRef(pair.second);
                }
                g_vm_callbacks.erase(it);
            }
        }
        lua_close(L);
    }
}

extern "C" JNIEXPORT jobject JNICALL
Java_io_github_luau_1jni_LuauVM_nativeExecute(JNIEnv* env, jobject thiz, jlong handle, jstring script) {
    lua_State* L = reinterpret_cast<lua_State*>(handle);
    if (!L) return luau_value_to_jobject(env, L, 0, thiz);

    std::string code = jstring_to_string(env, script);

    lua_CompileOptions opts = {};
    opts.optimizationLevel = 1;
    opts.debugLevel = 1;

    size_t bytecode_size = 0;
    char* bytecode = luau_compile(code.c_str(), code.length(), &opts, &bytecode_size);
    if (!bytecode) {
        LOGE("Luau compilation failed");
        return luau_value_to_jobject(env, L, 0, thiz);
    }

    int load_status = luau_load(L, "chunk", bytecode, bytecode_size, 0);
    free(bytecode);

    if (load_status != 0) {
        LOGE("Luau bytecode load failed: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
        return luau_value_to_jobject(env, L, 0, thiz);
    }

    int call_status = lua_pcall(L, 0, 1, 0);
    if (call_status != 0) {
        LOGE("Luau runtime error: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
        return luau_value_to_jobject(env, L, 0, thiz);
    }

    jobject res = luau_value_to_jobject(env, L, -1, thiz);
    lua_pop(L, 1);
    return res;
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_luau_1jni_LuauVM_nativeRegisterFunction(JNIEnv* env, jobject thiz, jlong handle, jstring name, jobject callback) {
    lua_State* L = reinterpret_cast<lua_State*>(handle);
    if (!L) return;

    std::string func_name = jstring_to_string(env, name);
    jobject global_callback = env->NewGlobalRef(callback);

    {
        std::lock_guard<std::mutex> lock(g_callbacks_mutex);
        g_vm_callbacks[L][func_name] = global_callback;
    }

    lua_pushstring(L, func_name.c_str());
    lua_pushcclosure(L, host_function_dispatcher, func_name.c_str(), 1);
    lua_setglobal(L, func_name.c_str());
}

extern "C" JNIEXPORT jobject JNICALL
Java_io_github_luau_1jni_LuauVM_nativeGetGlobal(JNIEnv* env, jobject thiz, jlong handle, jstring name) {
    lua_State* L = reinterpret_cast<lua_State*>(handle);
    if (!L) return luau_value_to_jobject(env, L, 0, thiz);

    std::string global_name = jstring_to_string(env, name);
    lua_getglobal(L, global_name.c_str());
    jobject res = luau_value_to_jobject(env, L, -1, thiz);
    lua_pop(L, 1);
    return res;
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_luau_1jni_LuauVM_nativeSetGlobal(JNIEnv* env, jobject thiz, jlong handle, jstring name, jobject value) {
    lua_State* L = reinterpret_cast<lua_State*>(handle);
    if (!L) return;

    std::string global_name = jstring_to_string(env, name);

    // Read LuauValue type
    jclass luau_val_cls = env->GetObjectClass(value);
    jfieldID type_fid = env->GetFieldID(luau_val_cls, "type", "Lio/github/luau_jni/LuauType;");
    jobject type_obj = env->GetObjectField(value, type_fid);

    jclass luau_type_cls = env->GetObjectClass(type_obj);
    jfieldID val_fid = env->GetFieldID(luau_type_cls, "value", "I");
    jint type_enum_val = env->GetIntField(type_obj, val_fid);

    env->DeleteLocalRef(luau_type_cls);
    env->DeleteLocalRef(type_obj);

    switch (type_enum_val) {
        case 0: lua_pushnil(L); break;
        case 1: {
            jmethodID as_bool = env->GetMethodID(luau_val_cls, "asBoolean", "(Z)Z");
            lua_pushboolean(L, env->CallBooleanMethod(value, as_bool, false));
            break;
        }
        case 3: {
            jmethodID as_dbl = env->GetMethodID(luau_val_cls, "asDouble", "(D)D");
            lua_pushnumber(L, env->CallDoubleMethod(value, as_dbl, 0.0));
            break;
        }
        case 4: {
            jmethodID as_str = env->GetMethodID(luau_val_cls, "asString", "(Ljava/lang/String;)Ljava/lang/String;");
            jstring js = (jstring)env->CallObjectMethod(value, as_str, nullptr);
            std::string s = jstring_to_string(env, js);
            if (js) env->DeleteLocalRef(js);
            lua_pushstring(L, s.c_str());
            break;
        }
        default: lua_pushnil(L); break;
    }

    env->DeleteLocalRef(luau_val_cls);
    lua_setglobal(L, global_name.c_str());
}

extern "C" JNIEXPORT jobject JNICALL
Java_io_github_luau_1jni_LuauVM_nativeGetTableFieldString(JNIEnv* env, jobject thiz, jlong handle, jlong refId, jstring key) {
    lua_State* L = reinterpret_cast<lua_State*>(handle);
    if (!L || refId == 0) return luau_value_to_jobject(env, L, 0, thiz);

    std::string key_str = jstring_to_string(env, key);
    lua_getref(L, static_cast<int>(refId));
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return luau_value_to_jobject(env, L, 0, thiz);
    }

    lua_getfield(L, -1, key_str.c_str());
    jobject res = luau_value_to_jobject(env, L, -1, thiz);
    lua_pop(L, 2);
    return res;
}

extern "C" JNIEXPORT jobject JNICALL
Java_io_github_luau_1jni_LuauVM_nativeGetTableFieldInt(JNIEnv* env, jobject thiz, jlong handle, jlong refId, jint index) {
    lua_State* L = reinterpret_cast<lua_State*>(handle);
    if (!L || refId == 0) return luau_value_to_jobject(env, L, 0, thiz);

    lua_getref(L, static_cast<int>(refId));
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return luau_value_to_jobject(env, L, 0, thiz);
    }

    lua_rawgeti(L, -1, index);
    jobject res = luau_value_to_jobject(env, L, -1, thiz);
    lua_pop(L, 2);
    return res;
}

extern "C" JNIEXPORT jobject JNICALL
Java_io_github_luau_1jni_LuauVM_nativeCallFunction(JNIEnv* env, jobject thiz, jlong handle, jlong refId, jobjectArray args) {
    lua_State* L = reinterpret_cast<lua_State*>(handle);
    if (!L || refId == 0) return luau_value_to_jobject(env, L, 0, thiz);

    lua_getref(L, static_cast<int>(refId));
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return luau_value_to_jobject(env, L, 0, thiz);
    }

    jsize arg_count = env->GetArrayLength(args);
    for (jsize i = 0; i < arg_count; ++i) {
        jobject arg_val = env->GetObjectArrayElement(args, i);
        if (!arg_val) {
            lua_pushnil(L);
            continue;
        }

        jclass luau_val_cls = env->GetObjectClass(arg_val);
        jfieldID type_fid = env->GetFieldID(luau_val_cls, "type", "Lio/github/luau_jni/LuauType;");
        jobject type_obj = env->GetObjectField(arg_val, type_fid);

        jclass luau_type_cls = env->GetObjectClass(type_obj);
        jfieldID val_fid = env->GetFieldID(luau_type_cls, "value", "I");
        jint type_enum_val = env->GetIntField(type_obj, val_fid);

        env->DeleteLocalRef(luau_type_cls);
        env->DeleteLocalRef(type_obj);

        switch (type_enum_val) {
            case 0: lua_pushnil(L); break;
            case 1: {
                jmethodID as_bool = env->GetMethodID(luau_val_cls, "asBoolean", "(Z)Z");
                lua_pushboolean(L, env->CallBooleanMethod(arg_val, as_bool, false));
                break;
            }
            case 3: {
                jmethodID as_dbl = env->GetMethodID(luau_val_cls, "asDouble", "(D)D");
                lua_pushnumber(L, env->CallDoubleMethod(arg_val, as_dbl, 0.0));
                break;
            }
            case 4: {
                jmethodID as_str = env->GetMethodID(luau_val_cls, "asString", "(Ljava/lang/String;)Ljava/lang/String;");
                jstring js = (jstring)env->CallObjectMethod(arg_val, as_str, nullptr);
                std::string s = jstring_to_string(env, js);
                if (js) env->DeleteLocalRef(js);
                lua_pushstring(L, s.c_str());
                break;
            }
            default: lua_pushnil(L); break;
        }

        env->DeleteLocalRef(luau_val_cls);
        env->DeleteLocalRef(arg_val);
    }

    int call_status = lua_pcall(L, arg_count, 1, 0);
    if (call_status != 0) {
        LOGE("Luau function call failed: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
        return luau_value_to_jobject(env, L, 0, thiz);
    }

    jobject res = luau_value_to_jobject(env, L, -1, thiz);
    lua_pop(L, 1);
    return res;
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_luau_1jni_LuauVM_nativeFreeRef(JNIEnv* env, jobject thiz, jlong handle, jlong refId) {
    lua_State* L = reinterpret_cast<lua_State*>(handle);
    if (L && refId != 0) {
        lua_unref(L, static_cast<int>(refId));
    }
}
