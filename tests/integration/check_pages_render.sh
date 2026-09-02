#!/usr/bin/env bash
# ============================================================================
# tests/integration/check_pages_render.sh
# Render every tracked .md the way the DOCS SITE does, and fail on anything
# that silently degrades.
#
# WHY A SECOND RENDERER. The README is read in three places and only one of
# them is github.com: the public repo's GitHub Pages site serves the same file
# through Jekyll, which parses with kramdown, not with GitHub's cmark-gfm. The
# two disagree, and when they do the page does not error - it just comes out
# wrong. The settings-tab table shipped that way in v1.29.3: correct on
# github.com, and on the site a single paragraph of pipes with the separator
# row's dashes typographed into em dashes, because an HTML comment sat in the
# table's block with no blank line before it. Nothing in this repo could have
# caught that, because everything we had looked at the file or at GitHub.
#
# WHY KRAMDOWN RATHER THAN A LINT. It is the renderer being modelled - the
# same gem Jekyll runs, with the same GFM input Pages configures - so this
# gate cannot drift from the thing it is protecting the way a hand-written
# rule about blank lines would. `check_docs.py` keeps a text version of the
# one rule we know, for the machine with no Ruby; this is the general case.
#
# WHAT IT ASSERTS, per file:
#   - kramdown parses it without raising.
#   - kramdown reports no warnings. That single signal covers the whole class:
#     an unclosed HTML tag swallowing a table row (`<tab>` in prose), a
#     bracketed literal read as a reference link (`[0,1]`, `[=]`), a link
#     definition that does not exist.
#   - no table fell back to a paragraph (the v1.29.3 bug's own shape).
#
# THE ONE EXEMPTION: CHANGELOG.md's newest heading. Released versions carry a
# `[x.y.z]: <compare url>` definition at the foot of the file and the pending
# one does not until it ships, so its heading is a reference link with nothing
# behind it - by design, and it renders as plain text meanwhile.
#
# SKIPS (exit 3, CTest reports SKIPPED) when no ruby on the box can load kramdown.
# See tools/install_deps.sh's `pages` group, and the interpreter search below for
# why "a ruby exists" is not the same question.
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/../.."

# THE INTERPRETER IS THE ONE THAT CAN LOAD THE GEMS, not whichever ruby PATH
# happens to resolve first. A hand-built or version-managed ruby ahead of the
# system one (rbenv, rvm, a container base image with /usr/local/bin/ruby) cannot
# see the apt gems, so the old check reported "kramdown not installed" naming the
# exact apt command that HAD already been run - and the gate skipped, which is
# not a pass. Following the message's own advice could not fix it. Trying the apt
# interpreter too costs one process and turns that dead end into a real answer.
RUBY=""
for candidate in ruby /usr/bin/ruby; do
    command -v "${candidate}" >/dev/null 2>&1 || continue
    "${candidate}" -e 'require "kramdown"; require "kramdown-parser-gfm"' >/dev/null 2>&1 || continue
    RUBY="${candidate}"
    break
done
if [ -z "${RUBY}" ]; then
    if command -v ruby >/dev/null 2>&1; then
        echo "SKIP: no ruby here can load kramdown / kramdown-parser-gfm."
        echo "      Install them (apt: ruby-kramdown ruby-kramdown-parser-gfm). If they ARE"
        echo "      installed, $(command -v ruby) is shadowing the system ruby and cannot see"
        echo "      its gems - put the apt ruby first on PATH, or gem install them for this one."
    else
        echo "SKIP: ruby not installed"
    fi
    exit 3
fi

# UTF-8: the docs carry arrows and box drawing, and kramdown refuses to parse a
# file whose bytes are not valid in the default external encoding.
RUBYOPT="-EUTF-8" "${RUBY}" -e '
require "kramdown"
require "kramdown-parser-gfm"

failures = 0
files = `git ls-files "*.md"`.split
abort("check_pages_render: git ls-files returned nothing") if files.empty?

files.each do |f|
  src = File.read(f, encoding: "UTF-8")
  begin
    doc  = Kramdown::Document.new(src, input: "GFM")
    html = doc.to_html
  rescue => e
    puts "FAIL #{f}: kramdown could not parse it - #{e.message}"
    failures += 1
    next
  end

  doc.warnings.each do |w|
    # See the header: the unreleased version heading has no link definition yet.
    next if f == "CHANGELOG.md" && w =~ /No link definition for link ID .\d+\.\d+\.\d+./
    puts "FAIL #{f}: #{w}"
    failures += 1
  end

  if html =~ /<p>\|/
    puts "FAIL #{f}: a table rendered as a paragraph of pipes. The block has to " \
         "be nothing but rows - put a blank line after the last one."
    failures += 1
  end
end

if failures > 0
  puts
  puts "#{failures} problem(s). These render correctly on github.com and wrongly on"
  puts "the docs site; a code span (`like this`) fixes every bracket and angle case."
  exit 1
end
puts "Pages render clean: #{files.size} markdown files through kramdown (GFM), no warnings, every table a table."
'
