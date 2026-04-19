plugins {
    id("com.android.application")
}

android {
    namespace = "com.aacp.demo"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.aacp.demo"
        minSdk = 24
        targetSdk = 34
        versionCode = 1
        versionName = "1.0"

        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }
    }

    buildTypes {
        debug {
            // giữ default
        }
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    packaging {
        // Nếu conflict .so có thể bật:
        // resources.pickFirsts.add("lib/**/libc++_shared.so")
    }
}

dependencies {
    // ✅ BẮT BUỘC để AAPT2 nhận các attribute layout_constraint*
    implementation("androidx.constraintlayout:constraintlayout:2.1.4")

    // (khuyến nghị) UI cơ bản
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("com.google.android.material:material:1.12.0")

    // Nếu module này phụ thuộc sdk:
    // implementation(project(":sdk"))
}