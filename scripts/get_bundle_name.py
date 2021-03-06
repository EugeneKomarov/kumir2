import re
import subprocess
import os
import os.path
import sys

if 3 == sys.version_info.major:
    from urllib.parse import unquote
    def to_str(x):
        return x.decode("utf-8")
else:
    from urllib import unquote
    def to_str(x):
        return unicode(x)


def get_version_information(top_level_dir):
    assert isinstance(top_level_dir, str) or isinstance(top_level_dir, unicode)
    if os.path.exists(top_level_dir + os.path.sep + ".git"):
        try:
            version_info = to_str(subprocess.check_output(
                "git describe --abbrev=0 --tags --exact-match",
                shell=True,
                stderr=subprocess.PIPE
            )).strip()
        except subprocess.CalledProcessError:
            version_info = to_str(subprocess.check_output(
                "git rev-parse --abbrev-ref HEAD",
                shell=True
            )).strip()
            version_info += "-" + to_str(subprocess.check_output(
                "git --no-pager log -1 --pretty=format:%H",
                shell=True
            )).strip()
    else:
        dir_name = os.path.basename(top_level_dir)
        match = re.match(r"kumir2-(.+)", dir_name)
        version_info = match.group(1)
    return version_info


def main():
    version_name = get_version_information(os.getcwd())
    prefix = ""
    suffix = ""
    nl = ""
    outfile = sys.stdout
    for arg in sys.argv:
        if arg.startswith("--prefix="):
            prefix = unquote(arg[9:].replace('@', '%'))
        elif arg.startswith("--suffix="):
            suffix = unquote(arg[9:].replace('@', '%'))
        elif arg.startswith("--out="):
            outfile = open(arg[6:], 'w')
        elif "--nl" == arg:
            if os.name.startswith("nt"):
                nl = "\r\n"
            else:
                nl = "\n"
    output = prefix + version_name + suffix + nl
    outfile.write(output)
    outfile.close()


if __name__ == "__main__":
    main()
