//! Admin HTTP (edge-http) — forms, JSON, SPA, routes, server.

pub mod form;
pub mod json;
pub mod routes;
pub mod server;
pub mod shared;
pub mod spa;
pub mod sse;

pub use server::run_admin_http;
pub use shared::HttpShared;
