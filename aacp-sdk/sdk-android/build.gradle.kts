plugins {
    alias(libs.plugins.android.library)
    alias(libs.plugins.kotlin.android)
}

android {
    namespace   = "com.aacp.sdk"
    compileSdk  = 35

    defaultConfig {
        minSdk = 29
        // Không cần versionCode/versionName ở đây cho Library
        
        ndk {
            abiFilters += listOf("arm64-v8a")
        }
    }

    // TRỎ ĐƯỜNG DẪN ĐẾN FILE .SO ĐÃ BUILD XONG
    sourceSets {
        getByName("main") {
            jniLibs.srcDirs("../outputs/jniLibs")
        }
    }

    // Đã gỡ bỏ externalNativeBuild (CMake) để tránh lỗi biên dịch lại

    buildTypes {
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
    kotlinOptions {
        jvmTarget = "17"
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.annotation)
}

// Task copy để gom file AAR ra ngoài cho tiện
tasks.register<Copy>("copyAarToOutputs") {
    dependsOn("assembleRelease")
    from(layout.buildDirectory.dir("outputs/aar"))
    into(rootProject.layout.projectDirectory.dir("outputs"))
    include("*-release.aar")
    rename { "aacp-sdk-1.0.0.aar" }
    doLast {
        println("✓ Đã tạo xong: outputs/aacp-sdk-1.0.0.aar")
    }
}