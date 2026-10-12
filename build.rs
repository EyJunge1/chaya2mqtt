//! Pack frontend/dist into a gzip asset blob for the device SPA (if present).

use std::env;
use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};

fn content_type(path: &str) -> &'static str {
    let lower = path.to_ascii_lowercase();
    if lower.ends_with(".html") {
        "text/html; charset=utf-8"
    } else if lower.ends_with(".js") || lower.ends_with(".mjs") {
        "application/javascript; charset=utf-8"
    } else if lower.ends_with(".css") {
        "text/css; charset=utf-8"
    } else if lower.ends_with(".svg") {
        "image/svg+xml"
    } else if lower.ends_with(".png") {
        "image/png"
    } else if lower.ends_with(".json") {
        "application/json"
    } else if lower.ends_with(".woff2") {
        "font/woff2"
    } else {
        "application/octet-stream"
    }
}

fn main() {
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let dist = manifest_dir
        .join("../../frontend/dist")
        .canonicalize()
        .ok();
    let out = PathBuf::from(env::var("OUT_DIR").unwrap());

    println!("cargo:rerun-if-changed=../../frontend/dist");
    println!("cargo:rerun-if-changed=src/web/assets/admin.html");

    let Some(dist) = dist.filter(|p| p.join("index.html").is_file()) else {
        write_empty(&out);
        return;
    };

    let mut entries: Vec<(String, Vec<u8>, &'static str)> = Vec::new();
    let index_html = fs::read_to_string(dist.join("index.html")).unwrap_or_default();

    walk(&dist, &dist, &mut entries);
    entries.sort_by(|a, b| a.0.cmp(&b.0));

    let mut blob = Vec::new();
    let mut meta = Vec::new();
    for (path, raw, ctype) in &entries {
        let gz = gzip_bytes(raw);
        let offset = blob.len();
        let len = gz.len();
        blob.extend_from_slice(&gz);
        meta.push((path.clone(), offset, len, *ctype));
    }

    fs::write(out.join("spa_blob.bin"), &blob).unwrap();
    let mut man = fs::File::create(out.join("spa_manifest.rs")).unwrap();
    writeln!(man, "pub const SPA_BLOB: &[u8] = include_bytes!(\"spa_blob.bin\");").unwrap();
    writeln!(man, "pub struct SpaAsset {{ pub path: &'static str, pub offset: usize, pub length: usize, pub content_type: &'static str, pub gzip: bool }}").unwrap();
    writeln!(man, "pub const SPA_ASSETS: &[SpaAsset] = &[").unwrap();
    for (path, offset, len, ctype) in &meta {
        writeln!(
            man,
            "  SpaAsset {{ path: \"{path}\", offset: {offset}, length: {len}, content_type: \"{ctype}\", gzip: true }},"
        )
        .unwrap();
    }
    writeln!(man, "];").unwrap();
    // escape index html for raw string
    let idx = index_html.replace('\\', "\\\\").replace('"', "\\\"");
    // use raw string if possible
    if !index_html.contains("\"##") {
        writeln!(
            man,
            "pub const SPA_INDEX_HTML: &str = r#\"{index_html}\"#;"
        )
        .unwrap();
    } else {
        writeln!(man, "pub const SPA_INDEX_HTML: &str = \"{idx}\";").unwrap();
    }
    writeln!(man, "pub const SPA_EMBEDDED: bool = true;").unwrap();
    let _ = idx;
}

fn write_empty(out: &Path) {
    fs::write(out.join("spa_blob.bin"), []).unwrap();
    let mut man = fs::File::create(out.join("spa_manifest.rs")).unwrap();
    writeln!(man, "pub const SPA_BLOB: &[u8] = &[];").unwrap();
    writeln!(man, "pub struct SpaAsset {{ pub path: &'static str, pub offset: usize, pub length: usize, pub content_type: &'static str, pub gzip: bool }}").unwrap();
    writeln!(man, "pub const SPA_ASSETS: &[SpaAsset] = &[];").unwrap();
    writeln!(man, "pub const SPA_INDEX_HTML: &str = include_str!(concat!(env!(\"CARGO_MANIFEST_DIR\"), \"/src/web/assets/admin.html\"));").unwrap();
    writeln!(man, "pub const SPA_EMBEDDED: bool = false;").unwrap();
}

fn walk(root: &Path, dir: &Path, out: &mut Vec<(String, Vec<u8>, &'static str)>) {
    let Ok(rd) = fs::read_dir(dir) else {
        return;
    };
    for e in rd.flatten() {
        let p = e.path();
        if p.is_dir() {
            walk(root, &p, out);
            continue;
        }
        let rel = p.strip_prefix(root).unwrap();
        let mut url = String::from("/");
        url.push_str(&rel.to_string_lossy().replace('\\', "/"));
        if url == "/index.html" {
            continue; // served as SPA_INDEX_HTML
        }
        let raw = fs::read(&p).unwrap_or_default();
        let ctype = content_type(&url);
        out.push((url, raw, ctype));
    }
}

fn gzip_bytes(raw: &[u8]) -> Vec<u8> {
    use flate2::Compression;
    use flate2::write::GzEncoder;
    let mut enc = GzEncoder::new(Vec::new(), Compression::best());
    enc.write_all(raw).unwrap();
    enc.finish().unwrap()
}
