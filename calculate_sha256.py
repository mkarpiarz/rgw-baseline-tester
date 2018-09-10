'''
Calculates the SHA256 checksum for an object of specified size.
'''
import sys
import hashlib

def main(argv):
    if len(argv) < 2:
        sys.stderr.write("Usage: %s <object_size>\n" % (argv[0],))
        return 1
    object_size = int(argv[1])
    object_content = '*'*object_size
    print( hashlib.sha256(object_content).hexdigest() );

if __name__ == '__main__':
    sys.exit(main(sys.argv))
