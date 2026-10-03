# Software typography contract

Software uses the canonical Infiltrator Common MB Corpo typography roles and packages only the three verified font faces required by those roles.

- `MB Corpo S Title WEB` regular (400) is the normal UI face.
- `MB Corpo S Title WEB` bold (700) is the UI emphasis, action and section-heading face.
- `MB Corpo A Title Cond WEB` regular (400) is reserved for deliberate condensed display-name roles.

Software must not request a 700-weight `MB Corpo A Title Cond WEB` face. No such bundled face exists; bold UI text must use the real MB Corpo S bold face rather than synthetic condensed bold rendering.

The source contract in `tests/source_contracts.py` enforces this mapping, while Common remains authoritative for family names, weights and immutable font-asset identity.
