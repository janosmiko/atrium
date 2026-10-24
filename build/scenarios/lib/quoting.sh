#!/usr/bin/env bash
# build/scenarios/lib/quoting.sh - argv quoting for scenario-authored text
# reaching vm_ssh (shared with keycombo.sh).

# ssh joins vm_ssh's argv with a plain space and hands the result to the
# remote login shell for a second parse, which local bash quoting cannot
# reach. vm_q and vm_num guard that second parse.

# Contract - the runner must define before sourcing:
#   die <msg>   abort the scenario (vm_num uses this to reject a bad field)

# vm_q <value> - wrap scenario-authored text in POSIX single quotes for the
# remote shell, escaping an embedded quote as '\''.
vm_q() {
  local v="$1"
  v="${v//\'/\'\\\'\'}"
  printf "'%s'" "${v}"
}

# vm_num <value> <field-name> - a field interpolated unquoted into a vm_ssh
# command must be a plain integer. A non-number is a broken scenario.
vm_num() {
  local v="$1" name="$2"
  [[ "${v}" =~ ^-?[0-9]+$ ]] || die "scenario field '${name}' is not a number: '${v}'"
}
