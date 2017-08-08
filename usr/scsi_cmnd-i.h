
static inline void scsi_set_result(struct scsi_cmd *scmd, int val) { scmd->result = val; } 
static inline int scsi_get_result(struct scsi_cmd *scmd) { return scmd->result; };
static inline void scsi_set_data_dir(struct scsi_cmd *scmd, enum data_direction val) { scmd->data_dir = val; }
static inline enum data_direction scsi_get_data_dir(struct scsi_cmd *scmd) { return scmd->data_dir; };



static inline void scsi_set_in_length(struct scsi_cmd *scmd, uint32_t val) { scmd->in_sdb.length = (val); } 
static inline uint32_t scsi_get_in_length(struct scsi_cmd *scmd) { return (scmd->in_sdb.length); } 
static inline void scsi_set_out_length(struct scsi_cmd *scmd, uint32_t val) { scmd->out_sdb.length = (val); } 
static inline uint32_t scsi_get_out_length(struct scsi_cmd *scmd) { return (scmd->out_sdb.length); };
static inline void scsi_set_in_transfer_len(struct scsi_cmd *scmd, uint32_t val) { scmd->in_sdb.transfer_len = (val); } 
static inline uint32_t scsi_get_in_transfer_len(struct scsi_cmd *scmd) { return (scmd->in_sdb.transfer_len); } 
static inline void scsi_set_out_transfer_len(struct scsi_cmd *scmd, uint32_t val) { scmd->out_sdb.transfer_len = (val); } 
static inline uint32_t scsi_get_out_transfer_len(struct scsi_cmd *scmd) { return (scmd->out_sdb.transfer_len); };
static inline void scsi_set_in_resid(struct scsi_cmd *scmd, int32_t val) { scmd->in_sdb.resid = (val); } 
static inline int32_t scsi_get_in_resid(struct scsi_cmd *scmd) { return (scmd->in_sdb.resid); } 
static inline void scsi_set_out_resid(struct scsi_cmd *scmd, int32_t val) { scmd->out_sdb.resid = (val); } 
static inline int32_t scsi_get_out_resid(struct scsi_cmd *scmd) { return (scmd->out_sdb.resid); };
static inline void scsi_set_in_buffer(struct scsi_cmd *scmd, void * val) { scmd->in_sdb.buffer = (unsigned long) (val); } 
static inline void * scsi_get_in_buffer(struct scsi_cmd *scmd) { return (void *)(unsigned long) (scmd->in_sdb.buffer); } 
static inline void scsi_set_out_buffer(struct scsi_cmd *scmd, void * val) { scmd->out_sdb.buffer = (unsigned long) (val); } 
static inline void * scsi_get_out_buffer(struct scsi_cmd *scmd) { return (void *)(unsigned long) (scmd->out_sdb.buffer); };


static inline void set_cmd_queued(struct scsi_cmd *c) { (c)->state |= (1UL << TGT_CMD_QUEUED); } 
static inline void clear_cmd_queued(struct scsi_cmd *c) { (c)->state &= ~(1UL << TGT_CMD_QUEUED); } 
static inline int cmd_queued(const struct scsi_cmd *c) { return ((c)->state & (1UL << TGT_CMD_QUEUED)); }
static inline void set_cmd_processed(struct scsi_cmd *c) { (c)->state |= (1UL << TGT_CMD_PROCESSED); } 
static inline void clear_cmd_processed(struct scsi_cmd *c) { (c)->state &= ~(1UL << TGT_CMD_PROCESSED); } 
static inline int cmd_processed(const struct scsi_cmd *c) { return ((c)->state & (1UL << TGT_CMD_PROCESSED)); }
static inline void set_cmd_async(struct scsi_cmd *c) { (c)->state |= (1UL << TGT_CMD_ASYNC); } 
static inline void clear_cmd_async(struct scsi_cmd *c) { (c)->state &= ~(1UL << TGT_CMD_ASYNC); } 
static inline int cmd_async(const struct scsi_cmd *c) { return ((c)->state & (1UL << TGT_CMD_ASYNC)); }
static inline void set_cmd_not_last(struct scsi_cmd *c) { (c)->state |= (1UL << TGT_CMD_NOT_LAST); } 
static inline void clear_cmd_not_last(struct scsi_cmd *c) { (c)->state &= ~(1UL <<

