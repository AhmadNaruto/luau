#include <jni.h>
#include <lua.h>
#include <lualib.h>
#include <luacode.h>
#include <string>

static std::string jstring_to_string(JNIEnv* env, jstring jstr) {
    if (!jstr) return "";
    const char* chars = env->GetStringUTFChars(jstr, nullptr);
    std::string str(chars);
    env->ReleaseStringUTFChars(jstr, chars);
    return str;
}

extern "C" JNIEXPORT jlong JNICALL
Java_io_github_luau_1jni_LuauVM_nativeCreate(JNIEnv* env, jobject thiz) {
    lua_State* L = luaL_newstate();
    if (L) {
        luaL_openlibs(L);
    }
    return reinterpret_cast<jlong>(L);
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_luau_1jni_LuauVM_nativeDestroy(JNIEnv* env, jobject thiz, jlong handle) {
    lua_State* L = reinterpret_cast<lua_State*>(handle);
    if (L) {
        lua_close(L);
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_io_github_luau_1jni_LuauVM_nativeExecute(JNIEnv* env, jobject thiz, jlong handle, jstring script) {
    lua_State* L = reinterpret_cast<lua_State*>(handle);
    if (!L) return env->NewStringUTF("Error: VM not initialized");

    std::string code = jstring_to_string(env, script);

    // Compile script to Luau bytecode
    size_t bytecode_size = 0;
    char* bytecode = luau_compile(code.c_str(), code.length(), nullptr, &bytecode_size);
    if (!bytecode) {
        return env->NewStringUTF("Error: Compilation failed");
    }

    // Load bytecode into Lua state
    int load_status = luau_load(L, "chunk", bytecode, bytecode_size, 0);
    free(bytecode);

    if (load_status != 0) {
        std::string err = lua_tostring(L, -1);
        lua_pop(L, 1);
        return env->NewStringUTF((std::string("Error loading bytecode: ") + err).c_str());
    }

    // Run bytecode
    int call_status = lua_pcall(L, 0, 1, 0);
    if (call_status != 0) {
        std::string err = lua_tostring(L, -1);
        lua_pop(L, 1);
        return env->NewStringUTF((std::string("Runtime error: ") + err).c_str());
    }

    // Get return value (as string)
    std::string result;
    if (lua_isstring(L, -1)) {
        result = lua_tostring(L, -1);
    } else {
        result = "success (non-string return)";
    }
    lua_pop(L, 1);

    return env->NewStringUTF(result.c_str());
}
