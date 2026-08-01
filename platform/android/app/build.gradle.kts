plugins {
    id("com.android.application")
}

android {
    namespace = "com.naragame.player"
    compileSdk = 37
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = providers.gradleProperty("naragame.applicationId")
            .getOrElse("com.naragame.player")
        minSdk = 26
        targetSdk = 37
        versionCode = 1
        versionName = providers.gradleProperty("naragame.versionName")
            .getOrElse("0.1.0")
        manifestPlaceholders["appName"] = providers
            .gradleProperty("naragame.appName").getOrElse("NaraGame")

        ndk {
            abiFilters += listOf("arm64-v8a")
        }
        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++20")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    sourceSets["main"].assets.srcDir("src/main/assets")
}
