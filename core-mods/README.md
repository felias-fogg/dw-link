##### Create a new Minicore release that contains debug support

All files necessary for modifying the core can be found in dw-link/core-mods

1. cd into MiniCore/avr (in branch master)
2. Run debugaddopt.py in this directory
3. Generate release 
4. Branch to gh-pages in MiniCore
5. Copy Add_dw-link-tools-release.sh to MiniCore (branch gh-pages) if not done already
6. Run this script, if not done at an earlier release!
7. Copy modified Boards_manager_release.sh from core-mods/ MiniCore-mods to MiniCore
8. Run this script
9. commit and push





##### New MiniCore release and how to update current Debug-MiniCore

1. After a new release has been made by MCUdude, copy the current package.json and the MiniCore-debug release tar balls from the gh-pagebranch to somewhere safe. 
2. Sync fork with upstream repo (removing the changes made by myself)
3. Run debugaddopt.py (in branch master in hardware folder)
4. Generate release (with smal "v" for version!)
5. Branch to gh-pages
6. Copy the additional parts from old package.json into the current one (releases and tools)
7. Copy the Minicore-debug releases tar balls  back to the gh-page
8. Copy modified Boards_manager_release.sh to gh-page branch
9. Run script
10. git add all the manually added files
11. git commit and push
