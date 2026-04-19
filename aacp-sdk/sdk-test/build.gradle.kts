plugins {
    alias(libs.plugins.android.app)
    alias(libs.plugins.kotlin.android)
}

android {
    namespace  = "com.aacp.demo"
    compileSdk = 35

    defaultConfig {
        applicationId   = "com.aacp.demo"
        minSdk          = 29
        targetSdk       = 35
        versionCode     = 1
        versionName     = "1.0"
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt")
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

    buildFeatures {
        viewBinding = true
    }
}

dependencies {
    // Depend trực tiếp vào sdk-android module (khi trong cùng project)
    implementation(project(":sdk-android"))

    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.appcompat)
}
