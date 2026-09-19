libyaml 0.2.5
=============

Copyright (c) 2017-2020 Ingy dot Net, (c) 2006-2016 Kirill Simonov. MIT License, see LICENSE.

Upstream: https://github.com/yaml/libyaml (release 0.2.5)

Only the parser side is vendored: api.c, reader.c, scanner.c, parser.c, yaml_private.h, yaml.h.
Emitter, writer, loader and dumper are left out.

Local changes: yaml_private.h includes "yaml.h" instead of <yaml.h> and carries default
YAML_VERSION_* macros in place of the generated config.h.
