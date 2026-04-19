plugins {
    alias(libs.plugins.android.library)
    alias(libs.plugins.kotlin.android)
}

android {
    namespace   = "com.aacp.sdk"
    compileSdk  = 35
    ndkVersion  = "27.0.12077973"

    defaultConfig {
        minSdk = 29   // Android 10 — cần cho AudioTrack.PERFORMANCE_MODE_LOW_LATENCY

        // Native build config
        externalNativeBuild {
            cmake {
                cppFlags    += listOf("-std=c++17", "-fexceptions", "-frtti")
                arguments   += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DANDROID_ARM_NEON=TRUE",
                    "-DANDROID=TRUE"              // Bật Android logging trong C++
                )
            }
        }
        // Chỉ build ARM64 — Pi4 là aarch64
        ndk {
            abiFilters += listOf("arm64-v8a")
        }

        // Version code cho .aar
        versionCode = 1
        versionName = "1.0.0"
    }

    // Native build entry point
    externalNativeBuild {
        cmake {
            path    = file("CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false  // Library không minify — consumer tự minify
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
            externalNativeBuild {
                cmake {
                    arguments += listOf("-DCMAKE_BUILD_TYPE=Release")
                }
            }
        }
        debug {
            externalNativeBuild {
                cmake {
                    arguments += listOf(
                        "-DCMAKE_BUILD_TYPE=Debug",
                        "-DAACP_VERBOSE_LOG=ON"
                    )
                }
            }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }

    // Cấu hình output .aar
    publishing {
        singleVariant("release") {
            withSourcesJar()
            withJavadocJar()
        }
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.annotation)
    // Không có runtime dependency nặng — SDK càng nhẹ càng tốt
}

// Task: copy .aar ra thư mục outputs/ ở root project
tasks.register<Copy>("copyAarToOutputs") {
    dependsOn("assembleRelease")
    from(layout.buildDirectory.dir("outputs/aar"))
    into(rootProject.layout.projectDirectory.dir("outputs"))
    include("*-release.aar")
    rename { "aacp-sdk-1.0.0.aar" }
    doLast {
        println("✓ AAR copied to outputs/aacp-sdk-1.0.0.aar")
    }
}
