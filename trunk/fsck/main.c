/*
	main.c (02.09.09)
	exFAT file system checker.

	Copyright (C) 2009, 2010  Andrew Nayenko

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <string.h>
#include <exfat.h>
#include <exfatfs.h>
#include <inttypes.h>

#define exfat_debug(format, ...)

#define MB (1024 * 1024)

#define BMAP_GET(bitmap, index) ((bitmap)[(index) / 8] & (1u << ((index) % 8)))

uint64_t files_count, directories_count;

static uint64_t bytes2mb(uint64_t bytes)
{
	return (bytes + MB / 2) / MB;
}

static void sbck(const struct exfat* ef)
{
	const uint32_t block_size = (1 << ef->sb->block_bits); /* in bytes */
	const uint32_t cluster_size = CLUSTER_SIZE(*ef->sb); /* in bytes */
	const uint64_t total = (uint64_t) le32_to_cpu(ef->sb->cluster_count) *
		cluster_size;

#if 0 /* low-level info */
	printf("First block           %8"PRIu64"\n",
			le64_to_cpu(ef->sb->block_start));
	printf("Blocks count          %8"PRIu64"\n",
			le64_to_cpu(ef->sb->block_count));
	printf("FAT first block       %8u\n",
			le32_to_cpu(ef->sb->fat_block_start));
	printf("FAT blocks count      %8u\n",
			le32_to_cpu(ef->sb->fat_block_count));
	printf("First cluster block   %8u\n",
			le32_to_cpu(ef->sb->cluster_block_start));
	printf("Clusters count        %8u\n",
			le32_to_cpu(ef->sb->cluster_count));
	printf("First cluster of root %8u\n",
			le32_to_cpu(ef->sb->rootdir_cluster));
#endif
	printf("Block size            %8u bytes\n", block_size);
	printf("Cluster size          %8u bytes\n", cluster_size);
	printf("Total space           %8"PRIu64" MB\n", bytes2mb(total));
	printf("Used space            %8hhu%%\n", ef->sb->allocated_percent);
}

static void nodeck(struct exfat* ef, struct exfat_node* node)
{
	const cluster_t cluster_size = CLUSTER_SIZE(*ef->sb);
	cluster_t clusters = (node->size + cluster_size - 1) / cluster_size;
	cluster_t c = node->start_cluster;
	
	while (clusters--)
	{
		if (CLUSTER_INVALID(c))
		{
			char name[EXFAT_NAME_MAX + 1];

			exfat_get_name(node, name, EXFAT_NAME_MAX);
			exfat_error("file `%s' has invalid cluster", name);
			return;
		}
		if (BMAP_GET(ef->cmap.chunk, c - EXFAT_FIRST_DATA_CLUSTER) == 0)
		{
			char name[EXFAT_NAME_MAX + 1];

			exfat_get_name(node, name, EXFAT_NAME_MAX);
			exfat_error("cluster 0x%x of file `%s' is not allocated", c, name);
		}
		c = exfat_next_cluster(ef, node, c);
	}
}

static void dirck(struct exfat* ef, const char* path)
{
	struct exfat_node* parent;
	struct exfat_node* node;
	struct exfat_iterator it;
	int rc;
	size_t path_length;
	char* entry_path;

	if (exfat_lookup(ef, &parent, path) != 0)
		exfat_bug("directory `%s' is not found", path);
	if (!(parent->flags & EXFAT_ATTRIB_DIR))
		exfat_bug("`%s' is not a directory (0x%x)", path, parent->flags);

	path_length = strlen(path);
	entry_path = malloc(path_length + 1 + EXFAT_NAME_MAX);
	if (entry_path == NULL)
	{
		exfat_error("out of memory");
		return;
	}
	strcpy(entry_path, path);
	strcat(entry_path, "/");

	rc = exfat_opendir(ef, parent, &it);
	if (rc != 0)
	{
		exfat_put_node(ef, parent);
		exfat_error("failed to open directory `%s'", path);
		return;
	}
	while ((node = exfat_readdir(ef, &it)))
	{
		exfat_get_name(node, entry_path + path_length + 1, EXFAT_NAME_MAX);
		exfat_debug("%s: %s, %llu bytes, cluster %u", subpath,
				IS_CONTIGUOUS(*node) ? "contiguous" : "fragmented",
				node->size, node->start_cluster);
		if (node->flags & EXFAT_ATTRIB_DIR)
		{
			directories_count++;
			dirck(ef, entry_path);
		}
		else
			files_count++;
		nodeck(ef, node);
		exfat_put_node(ef, node);
	}
	exfat_closedir(ef, &it);
	exfat_put_node(ef, parent);
	free(entry_path);
}

static void fsck(struct exfat* ef)
{
	sbck(ef);
	dirck(ef, "");
}

int main(int argc, char* argv[])
{
	struct exfat ef;

	if (argc != 2)
	{
		fprintf(stderr, "usage: %s <spec>\n", argv[0]);
		return 1;
	}
	printf("exfatck %u.%u.%u\n",
			EXFAT_VERSION_MAJOR, EXFAT_VERSION_MINOR, EXFAT_VERSION_PATCH);

	if (exfat_mount(&ef, argv[1], "") != 0)
		return 1;

	printf("Checking file system on %s.\n", argv[1]);
	fsck(&ef);
	exfat_unmount(&ef);
	printf("Totally %"PRIu64" directories and %"PRIu64" files.\n",
			directories_count, files_count);

	fputs("File system checking finished. ", stdout);
	if (exfat_errors != 0)
	{
		printf("ERRORS FOUND: %d.\n", exfat_errors);
		return 1;
	}
	puts("No errors found.");
	return 0;
}