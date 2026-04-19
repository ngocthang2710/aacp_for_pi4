// Root build file — chỉ chứa plugin declarations dùng chung
plugins {
    alias(libs.plugins.android.library)  apply false
    alias(libs.plugins.android.app)      apply false
    alias(libs.plugins.kotlin.android)   apply false
}

// Task tiện ích: clean toàn bộ outputs
tasks.register<Delete>("cleanOutputs") {
    delete(rootProject.layout.projectDirectory.dir("outputs"))
}
