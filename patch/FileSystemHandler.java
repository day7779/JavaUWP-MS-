/*
 * Vendored from tiny-remapper 0.8.2 (https://github.com/FabricMC/tiny-remapper),
 * which Fabric Loader shades into net.fabricmc.loader.impl.lib.tinyremapper.
 * build.ps1 rewrites the package back to net.fabricmc.tinyremapper when it
 * patches the standalone jar.
 * Licensed under the Apache License, Version 2.0.
 *
 * Modified for Xbox UWP: opens ZipFS without the URI route whose toRealPath call
 * the sandbox denies, and tolerates AccessDeniedException on close.
 */
package net.fabricmc.loader.impl.lib.tinyremapper;

import java.io.IOException;
import java.net.URI;
import java.nio.file.AccessDeniedException;
import java.nio.file.FileSystem;
import java.nio.file.FileSystemAlreadyExistsException;
import java.nio.file.FileSystems;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Collections;
import java.util.HashMap;
import java.util.IdentityHashMap;
import java.util.Map;

/**
 * Patched TinyRemapper FileSystemHandler for Xbox UWP.
 *
 * TinyRemapper 0.8.x opens input jars through FileSystems.newFileSystem(URI).
 * Xbox denies the ZipFS provider's URI path toRealPath call for both packaged
 * app files and LocalState files. The Path overload avoids that call.
 */
public final class FileSystemHandler {
    private static final Map<FileSystem, RefData> fsRefs = new IdentityHashMap<>();
    private static final Map<Path, FileSystem> fsByPath = new HashMap<>();

    private FileSystemHandler() {
    }

    public static synchronized FileSystem open(URI uri) throws IOException {
        Path path = jarUriToPath(uri);
        FileSystem fs = fsByPath.get(path);
        if (fs == null || !fs.isOpen()) {
            try {
                fs = FileSystems.newFileSystem(path, Collections.emptyMap());
            } catch (FileSystemAlreadyExistsException ignored) {
                fs = FileSystems.getFileSystem(URI.create("jar:" + path.toUri()));
            }
            fsByPath.put(path, fs);
            fsRefs.put(fs, new RefData(path, true, 1));
        } else {
            RefData data = fsRefs.get(fs);
            if (data == null) {
                fsRefs.put(fs, new RefData(path, false, 1));
            } else {
                data.refs++;
            }
        }

        return fs;
    }

    public static synchronized void close(FileSystem fs) throws IOException {
        RefData data = fsRefs.get(fs);
        if (data == null || data.refs <= 0) {
            throw new IllegalStateException("fs " + fs + " never opened via FileSystemHandler");
        }

        data.refs--;
        if (data.refs == 0) {
            fsRefs.remove(fs);
            if (data.path != null) {
                fsByPath.remove(data.path);
            }
            if (data.opened) {
                try {
                    fs.close();
                } catch (AccessDeniedException ex) {
                    if (!FileSystemReference.isZipFsRemoveRealPathFailure(ex)) {
                        throw ex;
                    }
                }
            }
        }
    }

    private static Path jarUriToPath(URI uri) {
        if ("jar".equalsIgnoreCase(uri.getScheme())) {
            String raw = uri.getRawSchemeSpecificPart();
            int sep = raw == null ? -1 : raw.indexOf("!/");
            if (sep >= 0) {
                raw = raw.substring(0, sep);
            }
            return Paths.get(URI.create(raw)).toAbsolutePath().normalize();
        }

        return Paths.get(uri).toAbsolutePath().normalize();
    }

    private static final class RefData {
        final Path path;
        final boolean opened;
        int refs;

        RefData(Path path, boolean opened, int refs) {
            this.path = path;
            this.opened = opened;
            this.refs = refs;
        }
    }
}
