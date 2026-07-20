package io.github.luau_jni

fun interface LuauFunction {
    fun invoke(args: List<LuauValue>): LuauValue
}
