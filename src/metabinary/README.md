# MetaBinarySystem
A simple low-level dynamic reflection module, port from MetaBinarySystem of [flame.cc](https://github.com/that-sky-project/flame.cc).

This module is used to hide low-level, assembly-adjacent details and to encapsulate game function hooking and calling, so that mod implementations that need to link into the game's internals look more like regular C++ programs.

At the same time, users or collaborators can use the descriptor provided by this module to conveniently switch between different game versions without recompiling the entire mod.

## Descriptor Schema

The composition of the descriptor file is as follows:

```jsonc
{
  "<edition_tag>": {
    "<arch_tag>": {
      "<name>": {
        "desc_type": "function",
        "desc_fallback": "<fallback>",
        "sig": [
          {
            "pattern": "<pattern>",
            "match": "<match_expr>"
          }
        ]
      }
    }
  }
}
```
