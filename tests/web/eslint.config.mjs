// ============================================================================
// tests/web/eslint.config.mjs — ESLint flat config for every .js in the repo.
//
// WHY IT LIVES HERE, not at the repo root: node_modules lives in tests/web (the
// only npm project in the tree), and a flat config's imports resolve relative to
// the CONFIG FILE — `import "@eslint/js"` from a root config would not resolve.
// lint.sh runs eslint from the repo root with --config, so the `files` globs
// below are repo-root-relative.
//
// WHY THE GATE EXISTS. The two dead-code nits fixed in the 1.28 release prep
// (an unused local, an unused for-in binding) were found by hand-running
// CodeQL's code-quality suite — a ~15-minute opt-in gate that runs the SECURITY
// suite in CI, so nothing would have caught them again. This is the standard
// tool for that job and it runs in about a second.
//
// The rule set is eslint's own `recommended`, minus three rules the shipped
// overlay's deliberate design makes unusable. Each is turned off with its
// reason, and only for the overlay — tests/web keeps the full set.
// ============================================================================
import js from "@eslint/js";

export default [
    {
        // Everything tests/web/.gitignore lists, plus the obvious build dirs.
        // Playwright's output is the load-bearing part and it bit once: a failed
        // run RETAINS test-results/<test>/, and a trace bundles third-party
        // browser JS as .js files — 64 errors from code nobody here wrote. Worse,
        // both gates carry the `fast` label, so `ctest -j N` runs this one WHILE
        // web-overlay is writing that directory. CI only stayed green by step
        // order (lint runs before the browser does), which is exactly the
        // local/CI asymmetry this gate exists to close. Globbed rather than
        // rooted at tests/web so an artifact written anywhere is still skipped.
        ignores: [
            "**/node_modules/**",
            "build/**",
            "mxbmrp3/vendor/**",
            "**/test-results/**",
            "**/playwright-report/**",
            "**/blob-report/**",
            "**/.cache/**",
        ],
    },

    js.configs.recommended,

    {
        // The shipped overlay: ordered classic scripts, ES5 on purpose (there is
        // no build step and users edit these files in place — see the header of
        // overlay-shell.js). Both exemptions below follow from that, and neither
        // is a judgement about the rule.
        files: ["mxbmrp3_data/web/**/*.js"],
        languageOptions: { ecmaVersion: 2020, sourceType: "script" },
        rules: {
            // The 11 files share ONE global scope, and ESLint sees one file at a
            // time — every cross-file call reads as undefined (967 of them). The
            // alternative is a hand-maintained globals list, i.e. a second copy
            // of every top-level name in the overlay, silently rotting.
            "no-undef": "off",
            // ES5 has no block scope, so a second `for (var i ...)` in a function
            // is the idiom, not a mistake. Enforcing it would push declarations
            // away from their use for no defect caught.
            "no-redeclare": "off",
            // `vars: "local"` is the same point as no-undef, from the other end:
            // a top-level function here is the overlay's public surface, used by
            // a sibling file ESLint isn't looking at. Locals are fully checked —
            // that is the class this gate was added for.
            // `caughtErrors: "none"`: ES5 has no optional catch binding, so an
            // unused `catch (e)` cannot be written any other way.
            "no-unused-vars": ["error",
                               { vars: "local", args: "none", caughtErrors: "none" }],
        },
    },

    {
        // The box-model dev tool (tools/boxmodel): boxmodel.js is UMD —
        // one file read by the page (browser), the fixture generator (node) and
        // the Playwright parity spec — so both environments' globals are real.
        // Not in the default gate targets (lint.sh lints mxbmrp3_data/web and
        // tests/web unless given a path), but linted clean so passing the path
        // works.
        files: ["tools/boxmodel/**/*.js"],
        languageOptions: {
            ecmaVersion: 2022, sourceType: "commonjs",
            globals: { module: "readonly", require: "readonly",
                       __dirname: "readonly", console: "readonly",
                       self: "readonly", window: "readonly" },
        },
        rules: { "no-unused-vars": ["error", { args: "none" }] },
    },

    {
        // The Playwright suite: modern CommonJS, linted at full strength. Its
        // in-page evaluate() callbacks reference browser and overlay globals, so
        // no-undef is off here for the same reason as above.
        files: ["tests/web/**/*.js"],
        languageOptions: { ecmaVersion: 2022, sourceType: "commonjs" },
        rules: {
            "no-undef": "off",
            "no-unused-vars": ["error", { args: "none" }],
        },
    },
];
