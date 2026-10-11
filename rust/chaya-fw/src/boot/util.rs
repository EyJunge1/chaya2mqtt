//! Boot helpers.

use alloc::boxed::Box;
use alloc::string::String;

pub fn leak(s: &str) -> &'static str {
    Box::leak(String::from(s).into_boxed_str())
}
