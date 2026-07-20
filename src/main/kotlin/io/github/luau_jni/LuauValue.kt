package io.github.luau_jni

class LuauValue internal constructor(
    val type: LuauType,
    private val boolValue: Boolean = false,
    private val doubleValue: Double = 0.0,
    private val stringValue: String? = null,
    private val mapValue: Map<String, LuauValue>? = null,
    private val listValue: List<LuauValue>? = null,
    val userdataPointer: Long = 0L,
    val userdataObject: Any? = null,
    internal val refId: Long = 0L,
    private val vm: LuauVM? = null
) : AutoCloseable {

    val isNil: Boolean get() = type == LuauType.NIL
    val isBoolean: Boolean get() = type == LuauType.BOOLEAN
    val isNumber: Boolean get() = type == LuauType.NUMBER
    val isString: Boolean get() = type == LuauType.STRING
    val isTable: Boolean get() = type == LuauType.TABLE
    val isFunction: Boolean get() = type == LuauType.FUNCTION
    val isUserdata: Boolean get() = type == LuauType.USERDATA || type == LuauType.LIGHTUSERDATA

    fun asString(default: String = ""): String = stringValue ?: default
    fun asDouble(default: Double = 0.0): Double = if (isNumber) doubleValue else default
    fun asInt(default: Int = 0): Int = if (isNumber) doubleValue.toInt() else default
    fun asLong(default: Long = 0L): Long = if (isNumber) doubleValue.toLong() else default
    fun asBoolean(default: Boolean = false): Boolean = if (isBoolean) boolValue else default

    fun optString(default: String = ""): String = asString(default)
    fun optBoolean(default: Boolean = false): Boolean = asBoolean(default)
    fun optInt(default: Int = 0): Int = asInt(default)
    fun optDouble(default: Double = 0.0): Double = asDouble(default)

    fun getTableMap(): Map<String, LuauValue> = mapValue ?: emptyMap()
    fun getTableList(): List<LuauValue> = listValue ?: emptyList()

    operator fun get(key: String): LuauValue = getField(key)
    operator fun get(index: Int): LuauValue = getField(index)

    fun getField(key: String): LuauValue {
        if (mapValue != null) return mapValue[key] ?: NIL
        if (vm != null && refId != 0L && type == LuauType.TABLE) {
            return vm.getTableField(refId, key)
        }
        return NIL
    }

    fun getField(index: Int): LuauValue {
        if (listValue != null) return listValue.getOrNull(index - 1) ?: NIL
        if (vm != null && refId != 0L && type == LuauType.TABLE) {
            return vm.getTableField(refId, index)
        }
        return NIL
    }

    fun call(vararg args: LuauValue): LuauValue {
        if (type == LuauType.FUNCTION && vm != null && refId != 0L) {
            return vm.callFunction(refId, args.toList())
        }
        return NIL
    }

    val length: Int
        get() {
            if (listValue != null) return listValue.size
            if (mapValue != null) return mapValue.size
            if (vm != null && refId != 0L && type == LuauType.TABLE) {
                var count = 0
                while (!getField(count + 1).isNil) {
                    count++
                }
                return count
            }
            return 0
        }

    override fun close() {
        if (vm != null && refId != 0L) {
            vm.freeRef(refId)
        }
    }

    companion object {
        val NIL = LuauValue(LuauType.NIL)
        val TRUE = LuauValue(LuauType.BOOLEAN, boolValue = true)
        val FALSE = LuauValue(LuauType.BOOLEAN, boolValue = false)

        fun valueOf(value: Boolean) = if (value) TRUE else FALSE
        fun valueOf(value: Double) = LuauValue(LuauType.NUMBER, doubleValue = value)
        fun valueOf(value: Int) = LuauValue(LuauType.NUMBER, doubleValue = value.toDouble())
        fun valueOf(value: Long) = LuauValue(LuauType.NUMBER, doubleValue = value.toDouble())
        fun valueOf(value: String?) = if (value == null) NIL else LuauValue(LuauType.STRING, stringValue = value)
        fun valueOf(map: Map<String, LuauValue>) = LuauValue(LuauType.TABLE, mapValue = map)
        fun valueOf(list: List<LuauValue>) = LuauValue(LuauType.TABLE, listValue = list)
        fun userdata(obj: Any?) = LuauValue(LuauType.USERDATA, userdataObject = obj)
        fun userdata(ptr: Long, obj: Any?) = LuauValue(LuauType.USERDATA, userdataPointer = ptr, userdataObject = obj)
        internal fun ref(type: LuauType, refId: Long, vm: LuauVM) = LuauValue(type, refId = refId, vm = vm)
    }
}
