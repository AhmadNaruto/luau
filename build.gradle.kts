plugins {
    alias(libs.plugins.android.library)
}

import com.android.build.api.dsl.LibraryExtension

extensions.configure<LibraryExtension> {
    namespace = "io.github.luau_jni"
    compileSdk = 37

    defaultConfig {
        minSdk = 26
        consumerProguardFiles("consumer-rules.pro")

        externalNativeBuild {
            cmake {
                // Compile Luau statically without CLI or tests
                arguments("-DLUAU_BUILD_CLI=OFF", "-DLUAU_BUILD_TESTS=OFF")
                
                // Targets arm64-v8a specifically (common for Android)
                abiFilters("arm64-v8a")
                
                // Compiler flags for C++17 support
                cppFlags("-std=c++17 -Wall -O2")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("CMakeLists.txt")
            version = "3.22.1"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_21
        targetCompatibility = JavaVersion.VERSION_21
    }
}

kotlin {
    jvmToolchain(21)
}
