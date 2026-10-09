#ifndef LOC_VERSION_H
#define LOC_VERSION_H

#define LOC_TITLE "LORDS OF CHAOS"

/* Release tag, shown on the title screen. Keep it equal to the git tag of the
 * release; a build can override it with -DLOC_VERSION='"v0.2.0"'. */
#ifndef LOC_VERSION
#define LOC_VERSION "v0.1.1"
#endif

#endif
