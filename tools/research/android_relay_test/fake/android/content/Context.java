package android.content;
public class Context {
    private final java.io.File dir;
    public Context(java.io.File d) { dir = d; }
    public java.io.File getFilesDir() { return dir; }
}
