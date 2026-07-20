package io.github.luau_jni

enum class LuauType(val value: Int) {
    NONE(-1),
    NIL(0),
    BOOLEAN(1),
    LIGHTUSERDATA(2),
    NUMBER(3),
    STRING(4),
    TABLE(5),
    FUNCTION(6),
    USERDATA(7),
    THREAD(8),
    BUFFER(9);

    companion object {
        fun fromInt(value: Int): LuauType = values().firstOrNull { it.value == value } ?: NONE
    }
}
