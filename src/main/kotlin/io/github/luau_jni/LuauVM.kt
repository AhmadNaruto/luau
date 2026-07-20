package io.github.luau_jni

class LuauVM : AutoCloseable {

    private var nativeHandle: Long = 0L

    init {
        nativeHandle = nativeCreate()
        if (nativeHandle == 0L) {
            throw RuntimeException("Failed to initialize Luau VM")
        }
    }

    fun execute(script: String): String {
        checkOpen()
        return nativeExecute(nativeHandle, script)
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
    private external fun nativeExecute(handle: Long, script: String): String

    companion object {
        init {
            System.loadLibrary("luau_jni")
        }
    }
}
