# Proguard rules for luau-jni library.
# These rules are automatically consumed by app modules using this library.

# Prevent obfuscation or removal of native methods and their class definitions
-keepclasseswithmembernames class * {
    native <methods>;
}

# Explicitly keep our wrapper classes and their members
-keep class io.github.luau_jni.LuauVM { *; }
-keep class io.github.luau_jni.LuauValue { *; }
-keep class io.github.luau_jni.LuauType { *; }
-keep class io.github.luau_jni.LuauFunction { *; }
