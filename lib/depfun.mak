# This is Bisqwit's generic depfun.mak, included from Makefile.
# The same file is used in many different projects.
#
# depfun.mak version 1.6.0
#
# Required vars:
#
#  ${CPP}        - C preprocessor name, usually "gcc"
#  ${CPPFLAGS}   - preprocessor flags (including defs)
#
#  ${ARCHFILES}  - All file names to include in archive
#                  .depend, depfun.mak and Makefile are
#                  automatically included.
#  ${ARCHNAME}   - Name of program. Example: testprog-0.0.1
#
# Optional vars:
#
#  ${ARCHDIR}       - Directory for the archives.
#                     Must end with '/'.
#  ${INSTALLPROGS}  - Programs to be installed (space delimited)
#  ${BINDIR}        - Directory for installed programs (without /)
#                     Example: /usr/local/bin
#  ${INSTALL}       - Installer program, example: install
#  ${DEPDIRS}       - Optional dependency dirs to account in .depend
#
#  ${EXTRA_ARCHFILES} - More files to include in archive,
#                       but without dependency checking


# Note: This requires perl. FIXME change it to sed
.depend: ${ARCHFILES}
	@echo "Checking dependencies..."
	@rm -f $@.tmp
	@for dir in "" ${DEPDIRS}; \
	 do n="`pwd`";\
	    if [ ! -z "$$dir" ]; then cd "$$dir"; fi; \
	    for s in *.c *.cc *.cpp; \
	    do if echo "$$s"|grep -vq '^\*';\
	       then \
	       cd "$$n";\
	       ${CPP} ${CPPFLAGS} -MM -MG "$$dir""$$s" |\
	        perl -pe "s|^([^ ])|$$dir\\1|" \
	         > $@."$$s";\
	    fi&done; \
	    cd "$$n"; \
	 done; wait
	@touch $@.dummy
	@cat $@.* >$@
	@cp -f $@ $@.tmp
	@sed 's/\.o:/.lo:/' <$@.tmp >>$@
	@rm -f $@.*

depend dep: .depend


-include .depend

git_rm: ;
	@- git rm -fr --cached '*' &> /dev/null

git_add: ${ARCHFILES} ;
	git add --ignore-errors ${ARCHFILES} ${EXTRA_ARCHFILES} depfun.mak Makefile
	@if [ -f docmaker.php ]; then php -q docmaker.php ${ARCHNAME} > README.html; git add docmaker.php README.html; fi
	@if [ -f makediff.php ]; then git add makediff.php; fi

git_push_branch: ;
	sed 's@^ref: refs/heads/@@' < .git/HEAD > .git/PUSHED_HEAD

git_pop_branch: ;
	git checkout `cat .git/PUSHED_HEAD`
	
git_release: ${ARCHFILES} ;
	git commit         -a -m 'Release version ${VERSION} (dev)' || \
	git commit --amend -a -m 'Release version ${VERSION} (dev)' # commit in dev brach
	sed 's@^ref: refs/heads/@@' < .git/HEAD > .git/PUSHED_HEAD
	git checkout release || git checkout -b release
	 git merge --no-commit `cat .git/PUSHED_HEAD`
	 git rm -fr --cached '*' &> /dev/null
	 git add --ignore-errors ${ARCHFILES} ${EXTRA_ARCHFILES} depfun.mak Makefile
	 @if [ -f docmaker.php ]; then php -q docmaker.php ${ARCHNAME} > README.html; git add docmaker.php README.html; fi
	 @if [ -f makediff.php ]; then git add makediff.php; fi
	 git commit -a -m 'Release version ${VERSION}' # commit in release
	 @- mkdir ${ARCHDIR} 2>/dev/null
	 git-archive --format=tar --prefix=${ARCHNAME}/ HEAD > ${ARCHDIR}${ARCHNAME}.tar
	git checkout `cat .git/PUSHED_HEAD`
	@make arch_finish_pak
	@make omabin_link${DEPFUN_OMABIN}

git_test_release: ${ARCHFILES}
	git commit --allow-empty -a -m 'Test release ${VERSION} (dev)' # commit in dev branch
	git-update-server-info
	sed 's@^ref: refs/heads/@@' < .git/HEAD > .git/PUSHED_HEAD
	git checkout release || git checkout -b release
	 grep refs/heads/release .git/info/refs | sed 's/	.*//' > .git/RELEASE_HEAD
	 git merge --no-commit `cat .git/PUSHED_HEAD`
	 git rm -fr --cached '*' &> /dev/null
	 git add --ignore-errors ${ARCHFILES} ${EXTRA_ARCHFILES} depfun.mak Makefile
	 @if [ -f docmaker.php ]; then php -q docmaker.php ${ARCHNAME} > README.html; git add docmaker.php README.html; fi
	 @if [ -f makediff.php ]; then git add makediff.php; fi
	 git commit -a -m 'Test release' # commit in release
	 rm -rf test_release
	 git-archive --format=tar --prefix=test_release/ HEAD | tar xvf - | sed 's/^/	/'
	 git reset --hard `cat .git/RELEASE_HEAD`         # undo release AND dev release
	git checkout `cat .git/PUSHED_HEAD`
	git-gc --quiet
	@echo
	@echo ----------------------------------------------------------------------
	@echo 'Would-be release extracted to test_release/ -- go ahead and try it.'
	@echo ----------------------------------------------------------------------
	@echo

UNUSED_archpak: ${ARCHFILES} ;
	@if [ "${ARCHNAME}" = "" ]; then echo ARCHNAME not set\!;false;fi
	- mkdir ${ARCHNAME} ${ARCHDIR} 2>/dev/null
	cp --parents -lfr ${ARCHFILES} ${EXTRA_ARCHFILES} depfun.mak Makefile ${ARCHNAME}/ 2>&1 >/dev/null | while read line;do cp --parents -fr "`echo "$$line"|sed 's/.*${ARCHNAME}\///;s/'\''.*//'`" ${ARCHNAME}/; done
	- if [ -f docmaker.php ]; then php -q docmaker.php ${ARCHNAME} >README.html; ln -f docmaker.php README.html ${ARCHNAME}/;fi
	if [ -f makediff.php ]; then ln -f makediff.php ${ARCHNAME}/; fi
	#- rm -f ${ARCHDIR}${ARCHNAME}.zip
	#- zip -9rq ${ARCHDIR}${ARCHNAME}.zip ${ARCHNAME}/
	#- rar a ${ARCHDIR}${ARCHNAME}.rar -mm -m5 -r -s -inul ${ARCHNAME}/
	#tar cf ${ARCHDIR}${ARCHNAME}.tar ${ARCHNAME}/
	#
	find ${ARCHNAME}/ -type d > .paktmp.txt
	find ${ARCHNAME}/ -not -type d | rev | sort | rev >> .paktmp.txt
	#find ${ARCHNAME}/|/ftp/backup/bsort >.paktmp.txt
	tar -c --no-recursion -f ${ARCHDIR}${ARCHNAME}.tar -T.paktmp.txt
	rm -rf .paktmp.txt ${ARCHNAME}
	@make arch_finish_pak

arch_finish_pak:	
	- if [ "${NOBZIP2ARCHIVES}" = "" ]; then bzip2 -9 >${ARCHDIR}${ARCHNAME}.tar.bz2 < ${ARCHDIR}${ARCHNAME}.tar; fi
	if [ "${NOGZIPARCHIVES}" = "" ]; then gzip -f9 ${ARCHDIR}${ARCHNAME}.tar; fi
	rm -f ${ARCHDIR}${ARCHNAME}.tar

# Makes the packages of various types...
UNUSED_pak: archpak ;
	if [ -f makediff.php ]; then php -q makediff.php ${ARCHNAME} ${ARCHDIR} 1; fi

omabin_link${DEPFUN_OMABIN}:
	- @rm -f /WWW/src/arch/${ARCHNAME}.tar.{bz2,gz}
	- ln -f ${ARCHDIR}${ARCHNAME}.tar.{bz2,gz} /WWW/src/arch/
	if [ -f progdesc.php ]; then cp -p --remove-destination progdesc.php /WWW/src/.desc-$(subst /,,$(dir $(subst -,/,$(ARCHNAME)))).php 2>/dev/null || cp -fp progdesc.php /WWW/src/.desc-$(subst /,,$(dir $(subst -,/,$(ARCHNAME)))).php; fi

# This is Bisqwit's method to install the packages to web-server...
UNUSED_omabin${DEPFUN_OMABIN}: archpak
	if [ -f makediff.php ]; then php -q makediff.php ${ARCHNAME} ${ARCHDIR}; fi
	#- @rm -f /WWW/src/arch/${ARCHNAME}.{zip,rar,tar.{bz2,gz}}
	#- ln -f ${ARCHDIR}${ARCHNAME}.{zip,rar,tar.{bz2,gz}} /WWW/src/arch/
	@make omabin_link${DEPFUN_OMABIN}

install${DEPFUN_INSTALL}: ${INSTALLPROGS}
	- if [ ! "${BINDIR}" = "" ]; then mkdir --parents $(BINDIR) 2>/dev/null; mkdir $(BINDIR) 2>/dev/null; \
	   for s in ${INSTALLPROGS} ""; do if [ ! "$$s" = "" ]; then \
	     ${INSTALL} -c -s -o bin -g bin -m 755 "$$s" ${BINDIR}/"$$s";fi;\
	   done; \
	  fi; \
	  if [ ! "${MANDIR}" = "" ]; then mkdir --parents $(MANDIR) 2>/dev/null; mkdir $(MANDIR) 2>/dev/null; \
	   for s in ${INSTALLMANS} ""; do if [ ! "$$s" = "" ]; then \
	     ${INSTALL} -m 644 "$$s" ${MANDIR}/man"`echo "$$s"|sed 's/.*\.//'`"/"$$s";fi;\
	   done; \
	  fi
	
uninstall${DEPFUN_INSTALL} deinstall${DEPFUN_INSTALL}:
	for s in ${INSTALLPROGS}; do rm -f ${BINDIR}/"$$s";done
	- for s in ${INSTALLLIBS}; do rm -f ${LIBDIR}/"$$s";done
	for s in ${INSTALLMANS} ""; do if [ ! "$$s" = "" ]; then \
	  rm -f ${MANDIR}/man"`echo "$$s"|sed 's/.*\.//'`"/"$$s";fi;\
	done; \

.PHONY: pak dep depend archpak omabin \
	install${DEPFUN_INSTALL} \
	deinstall${DEPFUN_INSTALL} \
	uninstall${DEPFUN_INSTALL}
