package io.github.luau_jni

class LuauVM : AutoCloseable {

    private var nativeHandle: Long = 0L

    init {
        nativeHandle = nativeCreate()
        if (nativeHandle == 0L) {
            throw RuntimeException("Failed to initialize Luau VM")
        }
    }

    fun execute(script: String): LuauValue {
        checkOpen()
        return nativeExecute(nativeHandle, script)
    }

    fun registerFunction(name: String, callback: LuauFunction) {
        checkOpen()
        nativeRegisterFunction(nativeHandle, name, callback)
    }

    fun getGlobal(name: String): LuauValue {
        checkOpen()
        return nativeGetGlobal(nativeHandle, name)
    }

    fun setGlobal(name: String, value: LuauValue) {
        checkOpen()
        nativeSetGlobal(nativeHandle, name, value)
    }

    internal fun getTableField(refId: Long, key: String): LuauValue {
        checkOpen()
        return nativeGetTableFieldString(nativeHandle, refId, key)
    }

    internal fun getTableField(refId: Long, index: Int): LuauValue {
        checkOpen()
        return nativeGetTableFieldInt(nativeHandle, refId, index)
    }

    internal fun callFunction(refId: Long, args: List<LuauValue>): LuauValue {
        checkOpen()
        return nativeCallFunction(nativeHandle, refId, args.toTypedArray())
    }

    internal fun freeRef(refId: Long) {
        if (nativeHandle != 0L && refId != 0L) {
            nativeFreeRef(nativeHandle, refId)
        }
    }

    override fun close() {
        val handle = nativeHandle
        if (handle != 0L) {
            nativeHandle = 0L
            nativeDestroy(handle)
        }
    }

    private fun checkOpen() {
        if (nativeHandle == 0L) {
            throw IllegalStateException("LuauVM is closed")
        }
    }

    private external fun nativeCreate(): Long
    private external fun nativeDestroy(handle: Long)
    private external fun nativeExecute(handle: Long, script: String): LuauValue
    private external fun nativeRegisterFunction(handle: Long, name: String, callback: LuauFunction)
    private external fun nativeGetGlobal(handle: Long, name: String): LuauValue
    private external fun nativeSetGlobal(handle: Long, name: String, value: LuauValue)
    private external fun nativeGetTableFieldString(handle: Long, refId: Long, key: String): LuauValue
    private external fun nativeGetTableFieldInt(handle: Long, refId: Long, index: Int): LuauValue
    private external fun nativeCallFunction(handle: Long, refId: Long, args: Array<LuauValue>): LuauValue
    private external fun nativeFreeRef(handle: Long, refId: Long)

    companion object {
        init {
            System.loadLibrary("luau_jni")
        }
    }
}
