// What OpenClaw's Doctor reads to learn that a2 has no state to migrate.
//
// a2 keeps its own files under <state-dir>/a2/ (devices.json and
// conversations/), in the same format since 1.0.0, and nothing in
// openclaw.json that Doctor needs to rewrite. Without this module and the
// matching "doctorContract": { "stateMigrations": [] } in
// openclaw.plugin.json, OpenClaw 2026.11.6 leaves an update's
// "deferred-plugin-migration:a2" pending for good ("data/settings upgrade
// is unfinished"), and `openclaw plugins install --force` refuses with
// "Plugin config repair could not be inspected: a2". An empty list here,
// declared the same in the manifest, is OpenClaw's way of saying the plugin
// was inspected and is stateless. The list must match the manifest's, entry
// for entry, or Doctor refuses it.
//
// This module is loaded on its own, without the plugin's runtime, so it
// imports nothing.

export const stateMigrations: [] = [];
