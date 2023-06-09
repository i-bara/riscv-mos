#include <elf.h>
#include <env.h>
#include <mmu.h>
#include <pmap.h>
#include <printk.h>
#include <sched.h>
#include <asm/csrdef.h>

// The maximum number of available ASIDs.
// Our bitmap requires this to be a multiple of 32.
#define NASID 64

struct Env envs[NENV] __attribute__((aligned(PAGE_SIZE))); // All environments

struct Env *curenv = NULL;	      // the current env
static struct Env_list env_free_list; // Free list

// Invariant: 'env' in 'env_sched_list' iff. 'env->env_status' is 'RUNNABLE'.
struct Env_sched_list env_sched_list; // Runnable list

u_long base_pgdir;

static uint64_t delta_time = 30000L;
static uint64_t time = 20000000L;

static uint32_t asid_bitmap[NASID / 32] = {0}; // 64

/* Overview:
 *  Allocate an unused ASID.
 *
 * Post-Condition:
 *   return 0 and set '*asid' to the allocated ASID on success.
 *   return -E_NO_FREE_ENV if no ASID is available.
 */
static int asid_alloc(u_int *asid) {
	for (u_int i = 0; i < NASID; ++i) {
		int index = i >> 5;
		int inner = i & 31;
		if ((asid_bitmap[index] & (1 << inner)) == 0) {
			asid_bitmap[index] |= 1 << inner;
			*asid = i;
			return 0;
		}
	}
	return -E_NO_FREE_ENV;
}

/* Overview:
 *  Free an ASID.
 *
 * Pre-Condition:
 *  The ASID is allocated by 'asid_alloc'.
 *
 * Post-Condition:
 *  The ASID is freed and may be allocated again later.
 */
static void asid_free(u_int i) {
	int index = i >> 5;
	int inner = i & 31;
	asid_bitmap[index] &= ~(1 << inner);
}

/* Overview:
 *  This function is to make a unique ID for every env
 *
 * Pre-Condition:
 *  e should be valid
 *
 * Post-Condition:
 *  return e's envid on success
 */
u_int mkenvid(struct Env *e) {
	static u_int i = 0;
	return ((++i) << (1 + LOG2NENV)) | (e - envs);
}

/* Overview:
 *   Convert an existing 'envid' to an 'struct Env *'.
 *   If 'envid' is 0, set '*penv = curenv', otherwise set '*penv = &envs[ENVX(envid)]'.
 *   In addition, if 'checkperm' is non-zero, the requested env must be either 'curenv' or its
 *   immediate child.
 *
 * Pre-Condition:
 *   'penv' points to a valid 'struct Env *'.
 *
 * Post-Condition:
 *   return 0 on success, and set '*penv' to the env.
 *   return -E_BAD_ENV on error (invalid 'envid' or 'checkperm' violated).
 */
int envid2env(u_int envid, struct Env **penv, int checkperm) {
	struct Env *e;

	/* Step 1: Assign value to 'e' using 'envid'. */
	/* Hint:
	 *   If envid is zero, set 'penv' to 'curenv'.
	 *   You may want to use 'ENVX'.
	 */
	/* Exercise 4.3: Your code here. (1/2) */
	if (envid == 0) {
		*penv = curenv;
		return 0;
	} else {
		e = &envs[ENVX(envid)];
	}

	if (e->env_status == ENV_FREE || e->env_id != envid) {
		return -E_BAD_ENV;
	}

	/* Step 2: Check when 'checkperm' is non-zero. */
	/* Hints:
	 *   Check whether the calling env has sufficient permissions to manipulate the
	 *   specified env, i.e. 'e' is either 'curenv' or its immediate child.
	 *   If violated, return '-E_BAD_ENV'.
	 */
	/* Exercise 4.3: Your code here. (2/2) */
	if (checkperm) {
		if (!(e == curenv || e->env_parent_id == checkperm)) {
			return -E_BAD_ENV;
		}
	}

	/* Step 3: Assign 'e' to '*penv'. */
	*penv = e;
	return 0;
}

/* Overview:
 *   Map [va, va+size) of virtual address space to physical [pa, pa+size) in the 'pgdir'. Use
 *   permission bits 'perm | PTE_V' for the entries.
 *
 * Pre-Condition:
 *   'pa', 'va' and 'size' are aligned to 'PAGE_SIZE'.
 */
static void map_pages(u_long *pgdir, u_int asid, u_long pa, u_long va, u_long size, u_int perm) {

	assert(pa % PAGE_SIZE == 0);
	assert(va % PAGE_SIZE == 0);
	assert(size % PAGE_SIZE == 0);

	/* Step 1: Map virtual address space to physical address space. */
	for (u_long i = 0; i < size; i += PAGE_SIZE) {
		/*
		 * Hint:
		 *  Map the virtual page 'va + i' to the physical page 'pa + i' using 'page_insert'.
		 *  Use 'pa2page' to get the 'struct Page *' of the physical address.
		 */
		/* Exercise 3.2: Your code here. */
		// struct Page *pp = pa2page(pa + i);
		// printk("%016lx->%016lx\n", va + i, pa + i);
		map_page(pgdir, asid, va + i, pa + i, perm);
		// page_insert(pgdir, asid, pp, va + i, perm);

	}
}

/* Overview:
 *   Mark all environments in 'envs' as free and insert them into the 'env_free_list'.
 *   Insert in reverse order, so that the first call to 'env_alloc' returns 'envs[0]'.
 *
 * Hints:
 *   You may use these macro definitions below: 'LIST_INIT', 'TAILQ_INIT', 'LIST_INSERT_HEAD'
 */
void env_init(void) {
	int i;
	/* Step 1: Initialize 'env_free_list' with 'LIST_INIT' and 'env_sched_list' with
	 * 'TAILQ_INIT'. */
	/* Exercise 3.1: Your code here. (1/2) */
	LIST_INIT(&env_free_list);
	TAILQ_INIT(&env_sched_list);

	/* Step 2: Traverse the elements of 'envs' array, set their status to 'ENV_FREE' and insert
	 * them into the 'env_free_list'. Make sure, after the insertion, the order of envs in the
	 * list should be the same as they are in the 'envs' array. */

	/* Exercise 3.1: Your code here. (2/2) */
	for (i = NENV - 1; i >= 0; i--) {
		envs[i].env_status = ENV_FREE;
		LIST_INSERT_HEAD(&env_free_list, &envs[i], env_link);
	}

	/*
	 * We want to map 'UPAGES' and 'UENVS' to *every* user space with PTE_G permission (without
	 * PTE_D), then user programs can read (but cannot write) kernel data structures 'pages' and
	 * 'envs'.
	 *
	 * Here we first map them into the *template* page directory 'base_pgdir'.
	 * Later in 'env_setup_vm', we will copy them into each 'env_pgdir'.
	 */
	struct Page *p;
	panic_on(page_alloc(&p));
	p->pp_ref++;

	base_pgdir = 0;
	map_pages(&base_pgdir, 0, (u_long)pages, PAGES, ROUND(npage * sizeof(struct Page), PAGE_SIZE),
		    PTE_R | PTE_G | PTE_U);
	map_pages(&base_pgdir, 0, (u_long)envs, ENVS, ROUND(NENV * sizeof(struct Env), PAGE_SIZE),
		    PTE_R | PTE_G | PTE_U);
	map_pages(&base_pgdir, 0, 0x80000000, 0x80000000, 0x0000000004000000, PTE_R | PTE_W | PTE_X);
	map_pages(&base_pgdir, 0, 0x10001000, 0xb0001000, 0x0000000000008000, PTE_R | PTE_W | PTE_X);

	// for (u_long pa = KERNBASE + 0x0000000; pa < KERNBASE + MEMORY_SIZE; pa += PAGE_SIZE) {
	// 	if (pa2page(pa)->pp_ref != 1) {
	// 		printk("pa=%016lx  ref=%d\n", pa, pa2page(pa)->pp_ref);
	// 	}
	// 	assert(pa2page(pa)->pp_ref == 0);
	// }

	// halt(); // 此段用来测试 page_ref，所有的 page_ref 都会比原来多 1

	for (u_long pa = KERNBASE + 0x0000000; pa < KERNBASE + MEMORY_SIZE; pa += PAGE_SIZE) { // 减回 1 就行了
		pa2page(pa)->pp_ref--;
	}

	// printk("base is %016lx\n", base_pgdir);

	// debug_page(&base_pgdir);

	// printk("pa=%016lx  perm=%010b\n", get_pa(&base_pgdir, 0x81ffffff), get_perm(&base_pgdir, 0x802024fc));
	// debug_page_va(&base_pgdir, 0x82fff000);
	// printk("ppn=%016lx\n", SATP_MODE_SV39 | base_pgdir);

	// debug_page(&base_pgdir);


	// printk("satp=%016lx\n", satp);

	// u_long status;
	// asm volatile("csrr %0, sstatus" : "=r"(status) :);
	// asm volatile("csrw sscratch, %0" : : "r"(satp));
	// asm volatile("csrr %0, sscratch" : "=r"(satp) :);
	// printk("stap=%016lx\n", satp);
	
	// printk("status=%016lx\n", status);
	// u_long old_satp;
	// asm volatile("csrr %0, satp" : "=r"(old_satp) :);
	// printk("oldstap=%016lx\n", old_satp);
	// asm volatile("csrw satp, %0" : : "r"(base_pgdir >> 12));
	// printk("nyan!\n");
	// printk("base is %016lx\n", base_pgdir);
	// printk("%016lx!\n", *(u_long *)base_pgdir);
	// printk("%016lx!\n", *((u_long *)base_pgdir + 1));
	// printk("%016lx!\n", *((u_long *)base_pgdir + 2));
	// printk("%016lx!\n", *((u_long *)base_pgdir + 2) << 2);
	// printk("%016lx!\n", *((u_long *)base_pgdir + 3));
	// printk("%016lx!\n", *((u_long *)base_pgdir + 4));

	#ifdef SV32
	u_long satp = (SATP_MODE_SV32 & SATP_MODE) | ((base_pgdir >> 12) & SATP_PPN);
	asm volatile("csrw satp, %0" : : "r"(satp));
	#else // Sv39
	u_long satp = (SATP_MODE_SV39 & SATP_MODE) | ((base_pgdir >> 12) & SATP_PPN);
	asm volatile("csrw satp, %0" : : "r"(satp));
	#endif
	
	printk("page table is good\n");

	#if !defined(LAB) || LAB >= 5
		virtio_init();
	#endif

	asid_bitmap[0] |= 1; // 占用 asid == 0
}

/* Overview:
 *   Initialize the user address space for 'e'.
 */
static int env_setup_vm(struct Env *e) {
	/* Step 1:
	 *   Allocate a page for the page directory with 'page_alloc'.
	 *   Increase its 'pp_ref' and assign its kernel address to 'e->env_pgdir'.
	 *
	 * Hint:
	 *   You can get the kernel address of a specified physical page using 'page2kva'.
	 */
	// struct Page *p;
	// try(page_alloc(&p));
	// /* Exercise 3.3: Your code here. */
	// p->pp_ref++;
	// // TODO: pgdir
	// e->env_pgdir = (Pde *)page2kva(p);

	/* Step 2: Copy the template page directory 'base_pgdir' to 'e->env_pgdir'. */
	/* Hint:
	 *   As a result, the address space of all envs is identical in [UTOP, UVPT).
	 *   See include/mmu.h for layout.
	 */
	// TODO: share page
	// memcpy(e->env_pgdir + PDX(UTOP), base_pgdir + PDX(UTOP),
	//        sizeof(Pde) * (PDX(UVPT) - PDX(UTOP)));

	/* Step 3: Map its own page table at 'UVPT' with readonly permission.
	 * As a result, user programs can read its page table through 'UVPT' */
	// e->env_pgdir[PDX(UVPT)] = PADDR(e->env_pgdir) | PTE_V;
	// memcpy(e->env_pgdir + PDX(UTOP), base_pgdir + PDX(UTOP),
	//         sizeof(Pde) * (PDX(UVPT) - PDX(UTOP)));
	// e->env_pgdir[PDX(UVPT)] = PADDR(e->env_pgdir) | PTE_V;
	

	// printk("nyan!");
	alloc_pgdir(&e->env_pgdir);
	map_page(&e->env_pgdir, e->env_asid, PAGE_TABLE + (PAGE_TABLE >> PN_SHIFT) + (PAGE_TABLE >> (2 * PN_SHIFT)), e->env_pgdir, PTE_R | PTE_U); // 6.18 罪魁祸首是这里，忘记了映射页表
	// printk("pgdir is %016lx\n", e->env_pgdir);
	// printk("%016lx\n", (u_long *)e->env_pgdir);

	#ifdef RISCV32
	((u_long *)e->env_pgdir)[0x1fd] = ((u_long *)base_pgdir)[0x1fd] | PTE_V; // 映射 pages 和 envs
	((u_long *)e->env_pgdir)[0x1fe] = ((u_long *)base_pgdir)[0x1fe] | PTE_V;
	#else
	((u_long *)e->env_pgdir)[PENVS] = ((u_long *)base_pgdir)[PENVS] | PTE_V; // 这样不可以，因为标记不一样
	#endif

	// ((u_long *)e->env_pgdir)[PPT] = PA2PTE(e->env_pgdir) | PTE_V; 不可以这样自映射
	
	// ((u_long *)e->env_pgdir)[PENV] = e->env_pgdir | PTE_V;
	// printk("pgdir is %016lx\n", e->env_pgdir);
	// debug_page(&base_pgdir);
	// debug_page(&e->env_pgdir);
	
	// printk("pgdir is %016lx\n", e->env_pgdir);
	return 0;
}

/* Overview:
 *   Allocate and initialize a new env.
 *   On success, the new env is stored at '*new'.
 *
 * Pre-Condition:
 *   If the new env doesn't have parent, 'parent_id' should be zero.
 *   'env_init' has been called before this function.
 *
 * Post-Condition:
 *   return 0 on success, and basic fields of the new Env are set up.
 *   return < 0 on error, if no free env, no free asid, or 'env_setup_vm' failed.
 *
 * Hints:
 *   You may need to use these functions or macros:
 *     'LIST_FIRST', 'LIST_REMOVE', 'mkenvid', 'asid_alloc', 'env_setup_vm'
 *   Following fields of Env should be set up:
 *     'env_id', 'env_asid', 'env_parent_id', 'env_tf.regs[29]', 'env_tf.cp0_status',
 *     'env_user_tlb_mod_entry', 'env_runs'
 */
int env_alloc(struct Env **new, u_int parent_id) {
	// int r;
	struct Env *e;

	/* Step 1: Get a free Env from 'env_free_list' */
	/* Exercise 3.4: Your code here. (1/4) */
	if (LIST_EMPTY(&env_free_list)) {
		return -E_NO_FREE_ENV;
	}
	e = LIST_FIRST(&env_free_list);

	/* Step 3: Initialize these fields for the new Env with appropriate values:
	 *   'env_user_tlb_mod_entry' (lab4), 'env_runs' (lab6), 'env_id' (lab3), 'env_asid' (lab3),
	 *   'env_parent_id' (lab3)
	 *
	 * Hint:
	 *   Use 'asid_alloc' to allocate a free asid.
	 *   Use 'mkenvid' to allocate a free envid.
	 */
	e->env_pgdir = 0;

	e->env_user_tlb_mod_entry = 0; // for lab4
	e->env_runs = 0;	       // for lab6
	/* Exercise 3.4: Your code here. (3/4) */
	e->env_id = mkenvid(e);
	try(asid_alloc(&e->env_asid));
	e->env_parent_id = parent_id;

	/* Step 2: Call a 'env_setup_vm' to initialize the user address space for this new Env. */
	/* Exercise 3.4: Your code here. (2/4) */
	try(env_setup_vm(e));

	/* Step 4: Initialize the sp and 'cp0_status' in 'e->env_tf'. */
	// Timer interrupt (STATUS_IM4) will be enabled.
	// lab 2: sstatus
	e->env_tf.sie = SIE_UTIE;
	e->env_tf.sstatus = SSTATUS_UIE;
	// Keep space for 'argc' and 'argv'.
	e->env_tf.sscratch = USTACKTOP - sizeof(int) - sizeof(char **); // 改成了 sscratch // 栈指针是 2 而不是 29，忘了改......

	/* Step 5: Remove the new Env from env_free_list. */
	/* Exercise 3.4: Your code here. (4/4) */
	LIST_REMOVE(e, env_link);

	*new = e;
	return 0;
}

/* Overview:
 *   Load a page into the user address space of an env with permission 'perm'.
 *   If 'src' is not NULL, copy the 'len' bytes from 'src' into 'offset' at this page.
 *
 * Pre-Condition:
 *   'offset + len' is not larger than 'PAGE_SIZE'.
 *
 * Hint:
 *   The address of env structure is passed through 'data' from 'elf_load_seg', where this function
 *   works as a callback.
 *
 */
static int load_icode_mapper(void *data, u_long va, size_t offset, u_int perm, const void *src,
			     size_t len) {
	struct Env *env = (struct Env *)data;
	// struct Page *p;
	// int r;

	/* Step 1: Allocate a page with 'page_alloc'. */
	/* Exercise 3.5: Your code here. (1/2) */
	// try(page_alloc(&p));

	/* Step 2: If 'src' is not NULL, copy the 'len' bytes started at 'src' into 'offset' at this
	 * page. */
	// Hint: You may want to use 'memcpy'.
	// if (src != NULL) {
	// 	/* Exercise 3.5: Your code here. (2/2) */
	// 	memcpy((void *)(page2kva(p) + offset), src, len);

	// }

	/* Step 3: Insert 'p' into 'env->env_pgdir' at 'va' with 'perm'. */
	// printk("%016lx\n", va);
	if (is_mapped_page(&env->env_pgdir, va) == 0) {
		try(alloc_page_user(&env->env_pgdir, env->env_asid, va, perm));
	}
	
	// printk("%016lx\n", env->env_pgdir);
	// debug_page(&env->env_pgdir);
	u_long pa = get_pa(&env->env_pgdir, va);
	if (src != NULL) {
		// 测试代码是否导入成功
		#ifdef DEBUG_ELF
		printk("from %016lx to %016lx->%016lx(%d)\n", src, (void *)(va + offset), (void *)(pa + offset), len);
		#endif
		memcpy((void *)pa, src, len); // 6.17 修复漏洞：va 本身就带有 offset（其低位正是 offset），因此 pa 无需加 offset
		#ifdef DEBUG_ELF
		printk("%016lx\n", *(u_long *)pa);
		printk("%016lx\n", ((u_long *)pa)[1]);
		printk("%016lx\n", ((u_long *)pa)[2]);
		printk("%016lx\n", ((u_long *)pa)[3]);
		printk("%016lx\n", ((u_long *)pa)[4]);
		printk("%016lx\n", ((u_long *)pa)[5]);
		#endif
	}
	return 0;
	// return page_insert(env->env_pgdir, env->env_asid, p, va, perm);
}

/* Overview:
 *   Load program segments from 'binary' into user space of the env 'e'.
 *   'binary' points to an ELF executable image of 'size' bytes, which contains both text and data
 *   segments.
 */
static void load_icode(struct Env *e, const void *binary, size_t size) {
	/* Step 1: Use 'elf_from' to parse an ELF header from 'binary'. */
	#ifdef RISCV32
	const Elf32_Ehdr *ehdr = elf_from(binary, size);
	#else // riscv64
	const Elf64_Ehdr *ehdr = elf_from_64(binary, size);
	#endif

	if (!ehdr) {
		panic("bad elf at %x", binary);
	}

	/* Step 2: Load the segments using 'ELF_FOREACH_PHDR_OFF' and 'elf_load_seg'.
	 * As a loader, we just care about loadable segments, so parse only program headers here.
	 */
	#ifdef DEBUG_ELF
	printk("size=%d\n", size);
	printk("binary=%016lx\n", binary);
	#endif

	size_t ph_off;
	ELF_FOREACH_PHDR_OFF (ph_off, ehdr) {
		#ifdef DEBUG_ELF
		printk("elf!\n");
		#endif

		#ifdef RISCV32
		Elf32_Phdr *ph = (Elf32_Phdr *)(binary + ph_off);
		#else // riscv64
		Elf64_Phdr *ph = (Elf64_Phdr *)(binary + ph_off);
		#endif
		
		if (ph->p_type == PT_LOAD) {
			// 'elf_load_seg' is defined in lib/elfloader.c
			// 'load_icode_mapper' defines the way in which a page in this segment
			// should be mapped.
			#ifdef RISCV32
			panic_on(elf_load_seg(ph, binary + ph->p_offset, load_icode_mapper, e));
			#else // riscv64
			panic_on(elf_load_seg_64(ph, binary + ph->p_offset, load_icode_mapper, e));
			#endif
		}
	}
	

	/* Step 3: Set 'e->env_tf.cp0_epc' to 'ehdr->e_entry'. */
	/* Exercise 3.6: Your code here. */
	// lab 2: sepc
	e->env_tf.sepc = ehdr->e_entry;

}

/* Overview:
 *   Create a new env with specified 'binary' and 'priority'.
 *   This is only used to create early envs from kernel during initialization, before the
 *   first created env is scheduled.
 *
 * Hint:
 *   'binary' is an ELF executable image in memory.
 */
struct Env *env_create(const void *binary, size_t size, int priority) {
	struct Env *e;
	/* Step 1: Use 'env_alloc' to alloc a new env, with 0 as 'parent_id'. */
	/* Exercise 3.7: Your code here. (1/3) */
	env_alloc(&e, 0);

	/* Step 2: Assign the 'priority' to 'e' and mark its 'env_status' as runnable. */
	/* Exercise 3.7: Your code here. (2/3) */
	e->env_pri = priority;
	e->env_status = ENV_RUNNABLE;

	/* Step 3: Use 'load_icode' to load the image from 'binary', and insert 'e' into
	 * 'env_sched_list' using 'TAILQ_INSERT_HEAD'. */
	/* Exercise 3.7: Your code here. (3/3) */
	load_icode(e, binary, size);

	// printk("00400000->%016lx\n", get_pa(&e->env_pgdir, 0x400000)); 测试内存空间页面分配 (1/2)
	
	// struct Page *pp;
	// try(page_alloc(&pp));
	// e->env_pgdir = page2pa(pp); // 这个不应该写，因为 load_icode 的时候就分配了页目录

	#ifdef RISCV32
	for (u_long vpn1 = 0x200; vpn1 < 0x400; vpn1++) {
		((u_long *)e->env_pgdir)[vpn1] = ((u_long *)base_pgdir)[vpn1]; // 快速的映射！
	}
	#else
	((u_long *)e->env_pgdir)[2] = ((u_long *)base_pgdir)[2]; // 快速的映射！
	#endif

	// map_pages(&e->env_pgdir, e->env_asid, 0x80000000, 0x80000000, 0x0000000004000000, PTE_R | PTE_W | PTE_X); // map 物理地址，稍后可以优化
	TAILQ_INSERT_HEAD(&env_sched_list, e, env_sched_link);

	// printk("00400000->%016lx\n", get_pa(&e->env_pgdir, 0x400000)); 测试内存空间页面分配 (2/2)

	return e;
}

/* Overview:
 *  Free env e and all memory it uses.
 */
void env_free(struct Env *e) {
	// Pte *pt;
	// u_int pdeno, pteno, pa;

	/* Hint: Note the environment's demise.*/
	#ifdef DEBUG
	#if (DEBUG >= 1)
	printk("[%08x] free env %08x\n", curenv ? curenv->env_id : 0, e->env_id);
	#endif
	#endif


	// debug_pte(&cur_pgdir, 0x80200000L);
	// u_long pa1 = get_pa(&cur_pgdir, 0x80200000L);
	// struct Page *pp = pa2page(pa1);
	// printk("pp->pp_ref=%d\n", pp->pp_ref);

	asm volatile("csrw satp, %0" : : "r"(SATP_MODE_BARE & SATP_MODE)); // 必须先切换为裸机再摧毁页表！！
	asm volatile("sfence.vma x0, %0" : : "r"(e->env_asid));
	destroy_pgdir(&e->env_pgdir, e->env_asid);
	asid_free(e->env_asid);

	/* Hint: Flush all mapped pages in the user portion of the address space */
	// for (pdeno = 0; pdeno < PDX(UTOP); pdeno++) {
	// 	/* Hint: only look at mapped page tables. */
	// 	if (!(e->env_pgdir[pdeno] & PTE_V)) {
	// 		continue;
	// 	}
	// 	/* Hint: find the pa and va of the page table. */
	// 	pa = PTE_ADDR(e->env_pgdir[pdeno]);
	// 	pt = (Pte *)KADDR(pa);
	// 	/* Hint: Unmap all PTEs in this page table. */
	// 	for (pteno = 0; pteno <= PTX(~0); pteno++) {
	// 		if (pt[pteno] & PTE_V) {
	// 			page_remove(e->env_pgdir, e->env_asid,
	// 				    (pdeno << PDSHIFT) | (pteno << PGSHIFT));
	// 		}
	// 	}
	// 	/* Hint: free the page table itself. */
	// 	e->env_pgdir[pdeno] = 0;
	// 	page_decref(pa2page(pa));
	// 	/* Hint: invalidate page table in TLB */
	// 	tlb_invalidate(e->env_asid, UVPT + (pdeno << PGSHIFT));
	// }
	// /* Hint: free the page directory. */
	// page_decref(pa2page(PADDR(e->env_pgdir)));
	// /* Hint: free the ASID */
	// asid_free(e->env_asid);
	// /* Hint: invalidate page directory in TLB */
	// tlb_invalidate(e->env_asid, UVPT + (PDX(UVPT) << PGSHIFT));
	/* Hint: return the environment to the free list. */
	e->env_status = ENV_FREE;
	LIST_INSERT_HEAD((&env_free_list), (e), env_link);
	TAILQ_REMOVE(&env_sched_list, (e), env_sched_link);

	e = TAILQ_FIRST(&env_sched_list);
}

/* Overview:
 *  Free env e, and schedule to run a new env if e is the current env.
 */
void env_destroy(struct Env *e) {
	/* Hint: free e. */
	env_free(e);

	/* Hint: schedule to run a new environment. */
	if (curenv == e) {
		curenv = NULL;
		#ifdef DEBUG
		#if (DEBUG >= 1)
		printk("i am killed ... \n");
		#endif
		#endif
		schedule(1);
	}
}

/* Overview:
 *   This function is depended by our judge framework. Please do not modify it.
 */
static inline void pre_env_run(struct Env *e) {
#ifdef MOS_SCHED_MAX_TICKS
	static int count = 0;
	if (count > MOS_SCHED_MAX_TICKS) {
		printk("%4d: ticks exceeded the limit %d\n", count, MOS_SCHED_MAX_TICKS);
		halt();
	}
	printk("%4d: %08x\n", count, e->env_id);
	count++;
#endif
#ifdef MOS_SCHED_END_PC
	struct Trapframe *tf = (struct Trapframe *)KSTACKTOP - 1;
	// printk("tf=%016lx\n", tf);
	u_long epc = tf->sepc;
	if (epc == MOS_SCHED_END_PC) {
		printk("env %08x reached end pc: 0x%08x, $v0=0x%08x\n", e->env_id, epc,
		       tf->regs[2]);
		env_destroy(e);
		schedule(0);
	}
#endif
}

extern void env_pop_tf(struct Trapframe *tf, u_int asid) __attribute__((noreturn));

/* Overview:
 *   Switch CPU context to the specified env 'e'.
 *
 * Post-Condition:
 *   Set 'e' as the current running env 'curenv'.
 *
 * Hints:
 *   You may use these functions: 'env_pop_tf'.
 */
void env_run(struct Env *e) {
	assert(e->env_status == ENV_RUNNABLE);
	pre_env_run(e); // WARNING: DO NOT MODIFY THIS LINE!

	/* Step 1:
	 *   If 'curenv' is NULL, this is the first time through.
	 *   If not, we may be switching from a previous env, so save its context into
	 *   'curenv->env_tf' first.
	 */
	if (curenv) {
		curenv->env_tf = *((struct Trapframe *)KSTACKTOP - 1);
	}

	/* Step 2: Change 'curenv' to 'e'. */
	curenv = e;
	curenv->env_runs++; // lab6

	/* Step 3: Change 'cur_pgdir' to 'curenv->env_pgdir', switching to its address space. */
	/* Exercise 3.8: Your code here. (1/2) */
	cur_pgdir = curenv->env_pgdir;
	// printk("%08x\n", get_pa(cur_pgdir, 0x00400000));

	/* Step 4: Use 'env_pop_tf' to restore the curenv's saved context (registers) and return/go
	 * to user mode.
	 *
	 * Hint:
	 *  - You should use 'curenv->env_asid' here.
	 *  - 'env_pop_tf' is a 'noreturn' function: it restores PC from 'cp0_epc' thus not
	 *    returning to the kernel caller, making 'env_run' a 'noreturn' function as well.
	 */
	/* Exercise 3.8: Your code here. (2/2) */
	// print_env(e);
	// print_regs();
	// print_code(0x83ff8000, 0x83ff8010);
	// print_code(0x803fffe8, 0x80400000);
	// print_tff((struct Trapframe *)0x803fff50);
	// print_tff(&e->env_tf);
	// printk("%08x\n", e);
	// print_stackframe(10);
	// printk("pa=%08x\n", get_pa(e->env_pgdir, 0x7f3fdff8));
	// env_pop_tf(&e->env_tf, e->env_asid);
	// lab 2: tlb



	// printk("%08x\n", e->env_tf.sepc);
	// u_long pa = get_pa(&cur_pgdir, e->env_tf.sepc);
	// printk("%08x: %08x\n", pa, ((u_long *)pa)[0]);
	// printk("          %08x\n", ((u_long *)pa)[1]);
	// printk("          %08x\n", ((u_long *)pa)[2]);
	// printk("          %08x\n", ((u_long *)pa)[3]);
	asm volatile("csrw sepc, %0" : : "r"(e->env_tf.sepc));
	u_long status;
	asm volatile("csrr %0, sstatus" : "=r"(status));
	// printk("sstatus = %016lx\n", status);
	#ifdef RISCV32
	asm volatile("csrw sstatus, %0" : : "r"(status & 0xfffffeff));
	#else // riscv39
	asm volatile("csrw sstatus, %0" : : "r"(status & 0xfffffffffffffeff));
	#endif
	// asm volatile("csrr %0, sstatus" : "=r"(status));
	// printk("sstatus = %016lx\n", status);
	
	// debug_page(&e->env_pgdir);
	// printk("%x\n", e->env_id);
	// printk("%016lx\n", envs[1].env_pgdir);

	// debug_pte(&cur_pgdir, 0x80200000L);

	#ifdef RISCV32
	for (u_long vpn1 = 0x200; vpn1 < 0x400; vpn1++) {
		((u_long *)e->env_pgdir)[vpn1] = ((u_long *)base_pgdir)[vpn1]; // 快速的映射！
	}
	#else
	((u_long *)e->env_pgdir)[2] = ((u_long *)base_pgdir)[2]; // 快速的映射！不知道为什么摧毁摧毁后切换的下一个进程没有内核映射
	#endif

	#ifdef SV32
	asm volatile("csrw satp, %0" : : "r"((SATP_MODE_SV32 & SATP_MODE) | (((u_long)e->env_asid << 22) & SATP_ASID) | ((e->env_pgdir >> 12) & SATP_PPN)));
	#else // Sv39
	asm volatile("csrw satp, %0" : : "r"((SATP_MODE_SV39 & SATP_MODE) | (((u_long)e->env_asid << 44) & SATP_ASID) | ((e->env_pgdir >> 12) & SATP_PPN)));
	#endif
	
	asm volatile("sfence.vma x0, x0");
	
	// 一种简单的自映射方法，但是在 RISC-V 下不可以这样，因为这样无法访问页表，因为缺少 PTE_R
	// ((u_long *)e->env_pgdir)[3] = PA2PTE(e->env_pgdir) | PTE_V;

	// debug_page_user(&e->env_pgdir);
	// printk("%016lx\n", e->env_pgdir);

	#ifdef RISCV32
	((u_long *)e->env_pgdir)[0x1fd] = ((u_long *)base_pgdir)[0x1fd] | PTE_V; // 映射 pages 和 envs
	((u_long *)e->env_pgdir)[0x1fe] = ((u_long *)base_pgdir)[0x1fe] | PTE_V;
	#else
	((u_long *)e->env_pgdir)[4] = ((u_long *)base_pgdir)[4] | PTE_V; // 映射 pages 和 envs
	#endif
	
	// debug_page(&e->env_pgdir);

	// printk("%016lx\n", ((struct Env *)ENVS)[0].env_id);
	
	
	// 检查自映射，如果这两个相等就说明自映射成功（这个检查首先需要把 PTE_U 位去掉）
	// debug_page_user(&e->env_pgdir);
	// printk("%016lx\n", get_pa(&e->env_pgdir, 0x400360));
	// u_long pa = get_pa(&e->env_pgdir, 0x400360);
	// printk("%016lx\n", *(u_long *)pa);
	// printk("%016lx\n", ((u_long *)pa)[1]);
	// printk("%016lx\n", ((u_long *)pa)[2]);
	// printk("%016lx\n", ((u_long *)pa)[3]);
	// printk("%016lx\n", ((u_long *)pa)[4]);
	// printk("%016lx\n", ((u_long *)pa)[5]);

	extern char exc_gen_entry[];
	e->env_tf.stvec = (u_long)exc_gen_entry; // 啊啊啊忘记加异常返回地址了，怪不得总是 sret 就卡住了

	// printk("yield!\n");
	// debug_env();
	// print_tf(&e->env_tf);

	// print_tf(&e->env_tf);
	// e->env_tf.sepc = 0x0000000000400000;
	// halt();
	// printk("%016lx\n", PTE2PA(((u_long *)PAGE_TABLE)[0x400]));


	int r = sbi_set_timer(time); // 时间不能太短，如果 10000000L 就会立刻中断
	assert(r == 0);
	time += delta_time;
	// printk("timer=%d\n", r);

	// e->env_tf.sip &=~ SIP_STIP; // 不可以写入 sip，因为没用
	e->env_tf.sie |= SIE_STIE;
	e->env_tf.sstatus |= SSTATUS_SPIE; // 不可以 SIE，否则会立刻中断

	// u_long sip;					// sip 不可写入！只能通过 ecall 来修改 sip
	// asm volatile("csrr %0, sscratch" : "=r"(sip));
	// printk("sip=%016lx\n", sip);
	// asm volatile("csrw sscratch, %0" : : "r"(0));
	// asm volatile("csrr %0, sscratch" : "=r"(sip));
	// printk("sip=%016lx\n", sip);

	// asm volatile("csrr %0, sie" : "=r"(sip));
	// printk("sip=%016lx\n", sip);
	// asm volatile("csrw sie, %0" : : "r"(0));
	// asm volatile("csrr %0, sie" : "=r"(sip));
	// printk("sip=%016lx\n", sip);

	// asm volatile("csrr %0, sip" : "=r"(sip));
	// printk("sip=%016lx\n", sip);
	// asm volatile("csrw sip, %0" : : "r"(0));

	// u_long sip;
	// asm volatile("csrr %0, sip" : "=r"(sip));
	// printk("sip=%016lx\n", sip);

	// asm volatile("csrs sie, %0" : : "r"(SIE_STIE));
	// asm volatile("csrs sstatus, %0" : : "r"(SSTATUS_SIE));
	asm volatile("add sp, %0, zero" : : "r"(&e->env_tf));
	asm volatile("j ret_from_exception");

	panic("Reach env_run end");
	while (1);
}

void env_check() {

}

// void env_check() {
// 	struct Env *pe, *pe0, *pe1, *pe2;
// 	struct Env_list fl;
// 	u_long page_addr;
// 	/* should be able to allocate three envs */
// 	pe0 = 0;
// 	pe1 = 0;
// 	pe2 = 0;
// 	assert(env_alloc(&pe0, 0) == 0);
// 	assert(env_alloc(&pe1, 0) == 0);
// 	assert(env_alloc(&pe2, 0) == 0);

// 	assert(pe0);
// 	assert(pe1 && pe1 != pe0);
// 	assert(pe2 && pe2 != pe1 && pe2 != pe0);

// 	/* temporarily steal the rest of the free envs */
// 	fl = env_free_list;
// 	/* now this env_free list must be empty! */
// 	LIST_INIT(&env_free_list);

// 	/* should be no free memory */
// 	assert(env_alloc(&pe, 0) == -E_NO_FREE_ENV);

// 	/* recover env_free_list */
// 	env_free_list = fl;

// 	printk("pe0->env_id %d\n", pe0->env_id);
// 	printk("pe1->env_id %d\n", pe1->env_id);
// 	printk("pe2->env_id %d\n", pe2->env_id);

// 	assert(pe0->env_id == 2048);
// 	assert(pe1->env_id == 4097);
// 	assert(pe2->env_id == 6146);
// 	printk("env_init() work well!\n");

// 	/* 'UENVS' and 'UPAGES' should have been correctly mapped in *template* page directory
// 	 * 'base_pgdir'. */
// 	for (page_addr = 0; page_addr < npage * sizeof(struct Page); page_addr += PAGE_SIZE) {
// 		assert(va2pa(base_pgdir, UPAGES + page_addr) == PADDR(pages) + page_addr);
// 	}
// 	for (page_addr = 0; page_addr < NENV * sizeof(struct Env); page_addr += PAGE_SIZE) {
// 		assert(va2pa(base_pgdir, UENVS + page_addr) == PADDR(envs) + page_addr);
// 	}
// 	/* check env_setup_vm() work well */
// 	printk("pe1->env_pgdir %x\n", pe1->env_pgdir);

// 	assert(pe2->env_pgdir[PDX(UTOP)] == base_pgdir[PDX(UTOP)]);
// 	assert(pe2->env_pgdir[PDX(UTOP) - 1] == 0);
// 	printk("env_setup_vm passed!\n");

// 	printk("pe2`s sp register %x\n", pe2->env_tf.regs[29]);

// 	/* free all env allocated in this function */
// 	TAILQ_INSERT_TAIL(&env_sched_list, pe0, env_sched_link);
// 	TAILQ_INSERT_TAIL(&env_sched_list, pe1, env_sched_link);
// 	TAILQ_INSERT_TAIL(&env_sched_list, pe2, env_sched_link);

// 	env_free(pe2);
// 	env_free(pe1);
// 	env_free(pe0);

// 	printk("env_check() succeeded!\n");
// }

// void envid2env_check() {
// 	struct Env *pe, *pe0, *pe2;
// 	assert(env_alloc(&pe0, 0) == 0);
// 	assert(env_alloc(&pe2, 0) == 0);
// 	int re;
// 	pe2->env_status = ENV_FREE;
// 	re = envid2env(pe2->env_id, &pe, 0);

// 	assert(re == -E_BAD_ENV);

// 	pe2->env_status = ENV_RUNNABLE;
// 	re = envid2env(pe2->env_id, &pe, 0);

// 	assert(pe->env_id == pe2->env_id && re == 0);

// 	curenv = pe0;
// 	re = envid2env(pe2->env_id, &pe, 1);
// 	assert(re == -E_BAD_ENV);
// 	printk("envid2env() work well!\n");
// }

void debug_env() {
	printk("---------------------------------------env----------------------------------------\n");
	printk("| id        status       parent    asid      pgdir             priority  index   |\n");
	for (int i = 0; i < NENV; i++) {
		struct Env *e = &envs[i];
		if (e->env_id) {
			if (e == curenv) {
				printk("|*");
			} else {
				printk("| ");
			}
			printk("%08x  ", e->env_id, e->env_pri);
			if (e->env_status == ENV_FREE) {
				printk("free         ");
			} else if (e->env_status == ENV_RUNNABLE) {
				printk("runnable     ");
			} else if (e->env_status == ENV_NOT_RUNNABLE) {
				printk("not runnable ");
			}
			if (e->env_parent_id) {
				printk("%08x  ", e->env_parent_id);
			} else {
				printk("          ");
			}
			printk("%08x  ", e->env_asid);
			if (e->env_pgdir) {
				printk("%016lx  ", e->env_pgdir);
			} else {
				printk("          ");
			}
			printk("%-8x  ", e->env_pri);
			printk("%-8x|\n", e - envs);
		}
	}
	printk("----------------------------------------------------------------------------------\n");
}

void print_env(struct Env* e) {
	printk("------------------------------------print env-------------------------------------\n");
	if (!e) {
		printk("|                                no env!                                 |\n");
		return;
	}
	if (e->env_id) {
		printk("| id        status       parent    asid      pgdir             priority  index   |\n");
		if (e == curenv) {
			printk("|*");
		} else {
			printk("| ");
		}
		printk("%08x  ", e->env_id, e->env_pri);
		if (e->env_status == ENV_FREE) {
			printk("free         ");
		} else if (e->env_status == ENV_RUNNABLE) {
			printk("runnable     ");
		} else if (e->env_status == ENV_NOT_RUNNABLE) {
			printk("not runnable ");
		}
		if (e->env_parent_id) {
				printk("%08x  ", e->env_parent_id);
			} else {
				printk("          ");
			}
			printk("%08x  ", e->env_asid);
			if (e->env_pgdir) {
				printk("%016lx  ", e->env_pgdir);
			} else {
				printk("          ");
			}
			printk("%-8x  ", e->env_pri);
			printk("%-8x|\n", e - envs);
	}
	printk("----------------------------------------------------------------------------------\n");
}

void debug_sched() {
	printk("--------------------------------------sched---------------------------------------\n");
	printk("| id        status       parent    asid      pgdir             priority  index   |\n");
	struct Env *e;
	TAILQ_FOREACH (e, &env_sched_list, env_sched_link) {
		if (e->env_id) {
			if (e == curenv) {
				printk("|*");
			} else {
				printk("| ");
			}
			printk("%08x  ", e->env_id, e->env_pri);
			if (e->env_status == ENV_FREE) {
				printk("free         ");
			} else if (e->env_status == ENV_RUNNABLE) {
				printk("runnable     ");
			} else if (e->env_status == ENV_NOT_RUNNABLE) {
				printk("not runnable ");
			}
			if (e->env_parent_id) {
				printk("%08x  ", e->env_parent_id);
			} else {
				printk("          ");
			}
			printk("%08x  ", e->env_asid);
			if (e->env_pgdir) {
				printk("%016lx  ", e->env_pgdir);
			} else {
				printk("          ");
			}
			printk("%-8x  ", e->env_pri);
			printk("%-8x|\n", e - envs);
		}
	}
	printk("----------------------------------------------------------------------------------\n");
}

void debug_elf(const void *binary, size_t size) {
	const Elf32_Ehdr *ehdr = elf_from(binary, size);
	printk("--------------------------------------elf---------------------------------------\n");
	printk("|type      offset    vaddr     paddr     filesz    memsz     flags     align   |\n");
	printk("--------------------------------------------------------------------------------\n");
	size_t ph_off;
	ELF_FOREACH_PHDR_OFF (ph_off, ehdr) {
		Elf32_Phdr *ph = (Elf32_Phdr *)(binary + ph_off);
		printk("|");
		if (ph->p_type == PT_NULL) {
			printk("null      ");
		} else if (ph->p_type == PT_LOAD) {
			printk("load      ");
		} else if (ph->p_type == PT_DYNAMIC) {
			printk("dynamic   ");
		} else if (ph->p_type == PT_INTERP) {
			printk("interp    ");
		} else if (ph->p_type == PT_NOTE) {
			printk("note      ");
		} else if (ph->p_type == PT_SHLIB) {
			printk("shlib     ");
		} else if (ph->p_type == PT_PHDR) {
			printk("phdr      ");
		} else /* if (ph->p_type == PT_TLS) {
			printk("tls       ");
		} else */ if (ph->p_type == PT_LOOS) {
			printk("loos      ");
		} else if (ph->p_type == PT_HIOS) {
			printk("hios      ");
		} else if (ph->p_type == PT_LOPROC) {
			printk("loproc    ");
		} else if (ph->p_type == PT_HIPROC) {
			printk("hiproc    ");
		} else {
			printk("unknown   ");
		}
		printk("%08x  %08x  %08x  %08x  %08x  ", ph->p_offset, ph->p_vaddr, ph->p_paddr, ph->p_filesz, ph->p_memsz);
		printk("%08x  %08x", ph->p_flags, ph->p_align);
		printk("|\n");
	}
	printk("--------------------------------------------------------------------------------\n");
}

__attribute__((noinline)) void *get_pc() {
    void *pc;
    asm volatile ("add %0, ra, zero" : "=r"(pc));
    return pc;
}

void print_code(void *pc1, void *pc2) {
	for (u_long *pc = (u_long *)pc1; pc < (u_long *)pc2; pc++) {
		printk("%08x: %08x\n", pc, *pc);
	}
}