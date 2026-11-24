/* Turn-relay requests on Android (online campaign, src/turn_relay.c).
 *
 * The desktop game runs the system's curl program; Android has none, so the
 * two requests the relay thread makes go to the app's Java side instead
 * (com.hatdagger.carnageheart.Relay, HttpURLConnection with the system's
 * TLS). The return codes match curl -f, so turn_relay.c treats both alike:
 *   0   done
 *   22  the relay answered with an HTTP error (e.g. 404: that turn file is
 *       not there yet)
 *   7   could not reach the relay
 *
 * ch_android_relay_init() runs on SDL's main thread (a Java thread, so the
 * app's classes can be found there); the relay thread is a native thread and
 * is attached to the VM by SDL_GetAndroidJNIEnv() on first use.
 */
#include <SDL3/SDL.h>
#include <jni.h>
#include <stdio.h>

static jclass g_cls;
static jmethodID g_get, g_put;

static int clear_exception(JNIEnv *env)
{
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return 1;
    }
    return 0;
}

/* Find the class through the activity's class loader (works on any thread). */
static jclass find_app_class(JNIEnv *env, const char *dotted)
{
    jobject act = (jobject)SDL_GetAndroidActivity();
    if (!act) return NULL;
    jclass actCls = (*env)->GetObjectClass(env, act);
    jmethodID getCl = (*env)->GetMethodID(env, actCls, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject loader = getCl ? (*env)->CallObjectMethod(env, act, getCl) : NULL;
    jclass found = NULL;
    if (loader && !clear_exception(env)) {
        jclass clCls = (*env)->FindClass(env, "java/lang/ClassLoader");
        jmethodID load = (*env)->GetMethodID(env, clCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
        jstring name = (*env)->NewStringUTF(env, dotted);
        found = (jclass)(*env)->CallObjectMethod(env, loader, load, name);
        if (clear_exception(env)) found = NULL;
        (*env)->DeleteLocalRef(env, name);
        (*env)->DeleteLocalRef(env, clCls);
    }
    clear_exception(env);
    if (loader) (*env)->DeleteLocalRef(env, loader);
    (*env)->DeleteLocalRef(env, actCls);
    (*env)->DeleteLocalRef(env, act);
    return found;
}

void ch_android_relay_init(void)
{
    if (g_cls) return;
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    if (!env) return;
    jclass c = (*env)->FindClass(env, "com/hatdagger/carnageheart/Relay");
    if (clear_exception(env) || !c) c = find_app_class(env, "com.hatdagger.carnageheart.Relay");
    if (!c) {
        printf("[relay] android: Relay class not found; online play is off\n");
        return;
    }
    g_get = (*env)->GetStaticMethodID(env, c, "nativeGet", "(Ljava/lang/String;Ljava/lang/String;II)I");
    g_put = (*env)->GetStaticMethodID(env, c, "nativePut", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)I");
    if (clear_exception(env) || !g_get || !g_put) {
        printf("[relay] android: Relay methods not found; online play is off\n");
        (*env)->DeleteLocalRef(env, c);
        return;
    }
    g_cls = (jclass)(*env)->NewGlobalRef(env, c);
    (*env)->DeleteLocalRef(env, c);
}

int ch_android_http_get(const char *url, const char *out_path, int connect_s, int max_s)
{
    if (!g_cls) ch_android_relay_init();
    if (!g_cls) return 7;
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    if (!env) return 7;
    jstring ju = (*env)->NewStringUTF(env, url);
    jstring jo = (*env)->NewStringUTF(env, out_path);
    jint rc = (*env)->CallStaticIntMethod(env, g_cls, g_get, ju, jo, (jint)connect_s, (jint)max_s);
    if (clear_exception(env)) rc = 7;
    (*env)->DeleteLocalRef(env, ju);
    (*env)->DeleteLocalRef(env, jo);
    return (int)rc;
}

int ch_android_http_put(const char *url, const char *key, const char *in_path, int max_s)
{
    if (!g_cls) ch_android_relay_init();
    if (!g_cls) return 7;
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    if (!env) return 7;
    jstring ju = (*env)->NewStringUTF(env, url);
    jstring jk = (*env)->NewStringUTF(env, key);
    jstring ji = (*env)->NewStringUTF(env, in_path);
    jint rc = (*env)->CallStaticIntMethod(env, g_cls, g_put, ju, jk, ji, (jint)max_s);
    if (clear_exception(env)) rc = 7;
    (*env)->DeleteLocalRef(env, ju);
    (*env)->DeleteLocalRef(env, jk);
    (*env)->DeleteLocalRef(env, ji);
    return (int)rc;
}
