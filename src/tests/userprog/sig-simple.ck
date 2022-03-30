# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(sig-simple) begin
(child-sig) run
Signum: 1, Action: 0x80480a0
Signum: 2, Action: 0x80480a1
child-sig: exit(81)
(sig-simple) end
sig-simple: exit(0)
EOF
pass;
