// Lint the front-end sources. Type-aware, because the rules that earn their
// keep here -- unhandled promises, unsafe `any` creeping in from JSON.parse --
// cannot be checked without types.
//
// The emitted JS under .build/ is not linted: it is generated, and every
// finding there would be a finding about tsc rather than about this code.
import js from '@eslint/js';
import tseslint from 'typescript-eslint';

export default tseslint.config(
  {
    // .build/ is tsc output -- every finding there would be about tsc, not
    // about this code. This config file is tooling rather than page source and
    // is deliberately outside tsconfig's include list, so it cannot be
    // type-aware linted either.
    ignores: ['.build/**', 'node_modules/**', 'eslint.config.js'],
  },
  js.configs.recommended,
  ...tseslint.configs.recommendedTypeChecked,
  {
    languageOptions: {
      parserOptions: {
        projectService: true,
        tsconfigRootDir: import.meta.dirname,
      },
    },
    rules: {
      // The page builds markup as template strings and assigns innerHTML. That
      // is the design -- there is no framework here -- so the interpolation
      // rule would fire on almost every render function without finding a bug.
      '@typescript-eslint/restrict-template-expressions': 'off',

      // A floating promise on this page means an unreported failure, which is
      // exactly the class of bug that made the USB tool look like it worked.
      '@typescript-eslint/no-floating-promises': 'error',

      // Empty catch blocks are load-bearing here: a board that is rebooting
      // legitimately drops a request. Require a comment saying which, rather
      // than banning the pattern.
      'no-empty': ['error', { allowEmptyCatch: false }],
    },
  },
);
