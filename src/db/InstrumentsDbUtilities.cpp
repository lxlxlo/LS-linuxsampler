/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2007 Grigor Iliev                                       *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the Free Software           *
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,                *
 *   MA  02110-1301  USA                                                   *
 ***************************************************************************/

#include "InstrumentsDbUtilities.h"

#if HAVE_SQLITE3

#include <dirent.h>
#include <errno.h>
#include <ftw.h>

#include "../common/Exception.h"
#include "InstrumentsDb.h"

namespace LinuxSampler {

    void DbInstrument::Copy(const DbInstrument& Instr) {
        if (this == &Instr) return;

        InstrFile = Instr.InstrFile;
        InstrNr = Instr.InstrNr;
        FormatFamily = Instr.FormatFamily;
        FormatVersion = Instr.FormatVersion;
        Size = Instr.Size;
        Created = Instr.Created;
        Modified = Instr.Modified;
        Description = Instr.Description;
        IsDrum = Instr.IsDrum;
        Product = Instr.Product;
        Artists = Instr.Artists;
        Keywords = Instr.Keywords;
    }


    void DbDirectory::Copy(const DbDirectory& Dir) {
        if (this == &Dir) return;

        Created = Dir.Created;
        Modified = Dir.Modified;
        Description = Dir.Description;
    }

    SearchQuery::SearchQuery() {
        MinSize = -1;
        MaxSize = -1;
        InstrType = BOTH;
    }

    void SearchQuery::SetFormatFamilies(String s) {
        if (s.length() == 0) return;
        int i = 0;
        int j = s.find(',', 0);
        
        while (j != std::string::npos) {
            FormatFamilies.push_back(s.substr(i, j - i));
            i = j + 1;
            j = s.find(',', i);
        }
        
        if (i < s.length()) FormatFamilies.push_back(s.substr(i));
    }

    void SearchQuery::SetSize(String s) {
        String s2 = GetMin(s);
        if (s2.length() > 0) MinSize = atoll(s2.c_str());
        else MinSize = -1;
        
        s2 = GetMax(s);
        if (s2.length() > 0) MaxSize = atoll(s2.c_str());
        else MaxSize = -1;
    }

    void SearchQuery::SetCreated(String s) {
        CreatedAfter = GetMin(s);
        CreatedBefore = GetMax(s);
    }

    void SearchQuery::SetModified(String s) {
        ModifiedAfter = GetMin(s);
        ModifiedBefore = GetMax(s);
    }

    String SearchQuery::GetMin(String s) {
        if (s.length() < 3) return "";
        if (s.at(0) == '.' && s.at(1) == '.') return "";
        int i = s.find("..");
        if (i == std::string::npos) return "";
        return s.substr(0, i);
    }

    String SearchQuery::GetMax(String s) {
        if (s.length() < 3) return "";
        if (s.find("..", s.length() - 2) != std::string::npos) return "";
        int i = s.find("..");
        if (i == std::string::npos) return "";
        return s.substr(i + 2);
    }

    void ScanJob::Copy(const ScanJob& Job) {
        if (this == &Job) return;

        JobId = Job.JobId;
        FilesTotal = Job.FilesTotal;
        FilesScanned = Job.FilesScanned;
        Scanning = Job.Scanning;
        Status = Job.Status;
    }

    int JobList::AddJob(ScanJob Job) {
        if (Counter + 1 < Counter) Counter = 0;
        else Counter++;
        Job.JobId = Counter;
        Jobs.push_back(Job);
        if (Jobs.size() > 3) {
            std::vector<ScanJob>::iterator iter = Jobs.begin();
            Jobs.erase(iter);
        }
        return Job.JobId;
    }

    ScanJob& JobList::GetJobById(int JobId) {
        for (int i = 0; i < Jobs.size(); i++) {
            if (Jobs.at(i).JobId == JobId) return Jobs.at(i);
        }
        
        throw Exception("Invalid job ID: " + ToString(JobId));
    }
    
    bool AbstractFinder::IsRegex(String Pattern) {
        if(Pattern.find('?') != String::npos) return true;
        if(Pattern.find('*') != String::npos) return true;
        return false;
    }

    void AbstractFinder::AddSql(String Col, String Pattern, std::stringstream& Sql) {
        if (Pattern.length() == 0) return;

        if (IsRegex(Pattern)) {
            Sql << " AND " << Col << " regexp ?";
            Params.push_back(Pattern);
            return;
        }

        String buf;
        std::vector<String> tokens;
        std::vector<String> tokens2;
        std::stringstream ss(Pattern);
        while (ss >> buf) tokens.push_back(buf);

        if (tokens.size() == 0) {
            Sql << " AND " << Col << " LIKE ?";
            Params.push_back("%" + Pattern + "%");
            return;
        }

        bool b = false;
        for (int i = 0; i < tokens.size(); i++) {
            Sql << (i == 0 ? " AND (" : "");

            for (int j = 0; j < tokens.at(i).length(); j++) {
                if (tokens.at(i).at(j) == '+') tokens.at(i).at(j) = ' ';
            }

            ss.clear();
            ss.str("");
            ss << tokens.at(i);

            tokens2.clear();
            while (ss >> buf) tokens2.push_back(buf);

            if (b && tokens2.size() > 0) Sql << " OR ";
            if (tokens2.size() > 1) Sql << "(";
            for (int j = 0; j < tokens2.size(); j++) {
                if (j != 0) Sql << " AND ";
                Sql << Col << " LIKE ?";
                Params.push_back("%" + tokens2.at(j) + "%");
                b = true;
            }
            if (tokens2.size() > 1) Sql << ")";
        }
        if (!b) Sql << "0)";
        else Sql << ")";
    }

    DirectoryFinder::DirectoryFinder(SearchQuery* pQuery) : pDirectories(new std::vector<String>) {
        pStmt = NULL;
        this->pQuery = pQuery;
        std::stringstream sql;
        sql << "SELECT dir_name from instr_dirs WHERE dir_id!=0 AND parent_dir_id=?";

        if (pQuery->CreatedAfter.length() != 0) {
            sql << " AND created > ?";
            Params.push_back(pQuery->CreatedAfter);
        }
        if (pQuery->CreatedBefore.length() != 0) {
            sql << " AND created < ?";
            Params.push_back(pQuery->CreatedBefore);
        }
        if (pQuery->ModifiedAfter.length() != 0) {
            sql << " AND modified > ?";
            Params.push_back(pQuery->ModifiedAfter);
        }
        if (pQuery->ModifiedBefore.length() != 0) {
            sql << " AND modified < ?";
            Params.push_back(pQuery->ModifiedBefore);
        }

        AddSql("dir_name", pQuery->Name, sql);
        AddSql("description", pQuery->Description, sql);
        SqlQuery = sql.str();

        InstrumentsDb* idb = InstrumentsDb::GetInstrumentsDb();

        int res = sqlite3_prepare(idb->GetDb(), SqlQuery.c_str(), -1, &pStmt, NULL);
        if (res != SQLITE_OK) {
            throw Exception("DB error: " + ToString(sqlite3_errmsg(idb->GetDb())));
        }

        for(int i = 0; i < Params.size(); i++) {
            idb->BindTextParam(pStmt, i + 2, Params.at(i));
        }
    }
    
    DirectoryFinder::~DirectoryFinder() {
        if (pStmt != NULL) sqlite3_finalize(pStmt);
    }

    StringListPtr DirectoryFinder::GetDirectories() {
        return pDirectories;
    }
    
    void DirectoryFinder::ProcessDirectory(String Path, int DirId) {
        InstrumentsDb* idb = InstrumentsDb::GetInstrumentsDb();
        idb->BindIntParam(pStmt, 1, DirId);

        String s = Path;
        if(Path.compare("/") != 0) s += "/";
        int res = sqlite3_step(pStmt);
        while(res == SQLITE_ROW) {
            pDirectories->push_back(s + ToString(sqlite3_column_text(pStmt, 0)));
            res = sqlite3_step(pStmt);
        }
        
        if (res != SQLITE_DONE) {
            sqlite3_finalize(pStmt);
            throw Exception("DB error: " + ToString(sqlite3_errmsg(idb->GetDb())));
        }

        res = sqlite3_reset(pStmt);
        if (res != SQLITE_OK) {
            sqlite3_finalize(pStmt);
            throw Exception("DB error: " + ToString(sqlite3_errmsg(idb->GetDb())));
        }
    }

    InstrumentFinder::InstrumentFinder(SearchQuery* pQuery) : pInstruments(new std::vector<String>) {
        pStmt = NULL;
        this->pQuery = pQuery;
        std::stringstream sql;
        sql << "SELECT instr_name from instruments WHERE dir_id=?";

        if (pQuery->CreatedAfter.length() != 0) {
            sql << " AND created > ?";
            Params.push_back(pQuery->CreatedAfter);
        }
        if (pQuery->CreatedBefore.length() != 0) {
            sql << " AND created < ?";
            Params.push_back(pQuery->CreatedBefore);
        }
        if (pQuery->ModifiedAfter.length() != 0) {
            sql << " AND modified > ?";
            Params.push_back(pQuery->ModifiedAfter);
        }
        if (pQuery->ModifiedBefore.length() != 0) {
            sql << " AND modified < ?";
            Params.push_back(pQuery->ModifiedBefore);
        }
        if (pQuery->MinSize != -1) sql << " AND instr_size > " << pQuery->MinSize;
        if (pQuery->MaxSize != -1) sql << " AND instr_size < " << pQuery->MaxSize;

        if (pQuery->InstrType == SearchQuery::CHROMATIC) sql << " AND is_drum = 0";
        else if (pQuery->InstrType == SearchQuery::DRUM) sql << " AND is_drum != 0";

        if (pQuery->FormatFamilies.size() > 0) {
            sql << " AND (format_family=?";
            Params.push_back(pQuery->FormatFamilies.at(0));
            for (int i = 1; i < pQuery->FormatFamilies.size(); i++) {
                sql << "OR format_family=?";
                Params.push_back(pQuery->FormatFamilies.at(i));
            }
            sql << ")";
        }

        AddSql("instr_name", pQuery->Name, sql);
        AddSql("description", pQuery->Description, sql);
        AddSql("product", pQuery->Product, sql);
        AddSql("artists", pQuery->Artists, sql);
        AddSql("keywords", pQuery->Keywords, sql);
        SqlQuery = sql.str();

        InstrumentsDb* idb = InstrumentsDb::GetInstrumentsDb();

        int res = sqlite3_prepare(idb->GetDb(), SqlQuery.c_str(), -1, &pStmt, NULL);
        if (res != SQLITE_OK) {
            throw Exception("DB error: " + ToString(sqlite3_errmsg(idb->GetDb())));
        }

        for(int i = 0; i < Params.size(); i++) {
            idb->BindTextParam(pStmt, i + 2, Params.at(i));
        }
    }
    
    InstrumentFinder::~InstrumentFinder() {
        if (pStmt != NULL) sqlite3_finalize(pStmt);
    }
    
    void InstrumentFinder::ProcessDirectory(String Path, int DirId) {
        InstrumentsDb* idb = InstrumentsDb::GetInstrumentsDb();
        idb->BindIntParam(pStmt, 1, DirId);

        String s = Path;
        if(Path.compare("/") != 0) s += "/";
        int res = sqlite3_step(pStmt);
        while(res == SQLITE_ROW) {
            pInstruments->push_back(s + ToString(sqlite3_column_text(pStmt, 0)));
            res = sqlite3_step(pStmt);
        }
        
        if (res != SQLITE_DONE) {
            sqlite3_finalize(pStmt);
            throw Exception("DB error: " + ToString(sqlite3_errmsg(idb->GetDb())));
        }

        res = sqlite3_reset(pStmt);
        if (res != SQLITE_OK) {
            sqlite3_finalize(pStmt);
            throw Exception("DB error: " + ToString(sqlite3_errmsg(idb->GetDb())));
        }
    }

    StringListPtr InstrumentFinder::GetInstruments() {
        return pInstruments;
    }

    void DirectoryCounter::ProcessDirectory(String Path, int DirId) {
        count += InstrumentsDb::GetInstrumentsDb()->GetDirectoryCount(DirId);
    }

    void InstrumentCounter::ProcessDirectory(String Path, int DirId) {
        count += InstrumentsDb::GetInstrumentsDb()->GetInstrumentCount(DirId);
    }

    DirectoryCopier::DirectoryCopier(String SrcParentDir, String DestDir) {
        this->SrcParentDir = SrcParentDir;
        this->DestDir = DestDir;

        if (DestDir.at(DestDir.length() - 1) != '/') {
            this->DestDir.append("/");
        }
        if (SrcParentDir.at(SrcParentDir.length() - 1) != '/') {
            this->SrcParentDir.append("/");
        }
    }

    void DirectoryCopier::ProcessDirectory(String Path, int DirId) {
        InstrumentsDb* db = InstrumentsDb::GetInstrumentsDb();

        String dir = DestDir;
        String subdir = Path;
        if(subdir.length() > SrcParentDir.length()) {
            subdir = subdir.substr(SrcParentDir.length());
            dir += subdir;
            db->AddDirectory(dir);
        }

        int dstDirId = db->GetDirectoryId(dir);
        if(dstDirId == -1) throw Exception("Unkown DB directory: " + dir);
        IntListPtr ids = db->GetInstrumentIDs(DirId);
        for (int i = 0; i < ids->size(); i++) {
            String name = db->GetInstrumentName(ids->at(i));
            db->CopyInstrument(ids->at(i), name, dstDirId, dir);
        }
    }

    ScanProgress::ScanProgress() {
        TotalFileCount = ScannedFileCount = Status = 0;
        CurrentFile = "";
        GigFileProgress.custom = this;
        GigFileProgress.callback = GigFileProgressCallback;
    }

    void ScanProgress::StatusChanged() {
        InstrumentsDb* db = InstrumentsDb::GetInstrumentsDb();
        db->Jobs.GetJobById(JobId).FilesTotal = GetTotalFileCount();
        db->Jobs.GetJobById(JobId).FilesScanned = GetScannedFileCount();
        db->Jobs.GetJobById(JobId).Scanning = CurrentFile;
        db->Jobs.GetJobById(JobId).Status = GetStatus();
        
        InstrumentsDb::GetInstrumentsDb()->FireJobStatusChanged(JobId);
    }

    int ScanProgress::GetTotalFileCount() {
        return TotalFileCount;
    }

    void ScanProgress::SetTotalFileCount(int Count) {
        if (TotalFileCount == Count) return;
        TotalFileCount = Count;
        StatusChanged();
    }

    int ScanProgress::GetScannedFileCount() {
        return ScannedFileCount;
    }

    void ScanProgress::SetScannedFileCount(int Count) {
        if (ScannedFileCount == Count) return;
        ScannedFileCount = Count;
        if (Count > TotalFileCount) TotalFileCount = Count;
        StatusChanged();
    }

    int ScanProgress::GetStatus() {
        return Status;
    }

    void ScanProgress::SetStatus(int Status) {
        if (this->Status == Status) return;
        if (Status < 0) this->Status = 0;
        else if (Status > 100) this->Status = 100;
        else this->Status = Status;
        StatusChanged();
    }

    void ScanProgress::SetErrorStatus(int Err) {
        if (Err > 0) Err *= -1;
        Status = Err;
        StatusChanged();
    }

    void ScanProgress::GigFileProgressCallback(gig::progress_t* pProgress) {
        if (pProgress == NULL) return;
        ScanProgress* sp = static_cast<ScanProgress*> (pProgress->custom);
        
        sp->SetStatus((int)(pProgress->factor * 100));
    }

    AddInstrumentsJob::AddInstrumentsJob(int JobId, ScanMode Mode, String DbDir, String FsDir) {
        this->JobId = JobId;
        Progress.JobId = JobId;
        this->Mode = Mode;
        this->DbDir = DbDir;
        this->FsDir = FsDir;
    }

    void AddInstrumentsJob::Run() {
        try {
            InstrumentsDb* db = InstrumentsDb::GetInstrumentsDb();

            switch (Mode) {
                case NON_RECURSIVE:
                    Progress.SetTotalFileCount(GetFileCount());
                    db->AddInstrumentsNonrecursive(DbDir, FsDir, &Progress);
                    break;
                case RECURSIVE:
                    db->AddInstrumentsRecursive(DbDir, FsDir, false, &Progress);
                    break;
                case FLAT:
                    db->AddInstrumentsRecursive(DbDir, FsDir, true, &Progress);
                    break;
                default:
                    throw Exception("Unknown scan mode");
            }

            // Just to be sure that the frontends will be notified about the job completion
            if (Progress.GetTotalFileCount() != Progress.GetScannedFileCount()) {
                Progress.SetTotalFileCount(Progress.GetScannedFileCount());
            }
            if (Progress.GetStatus() != 100) Progress.SetStatus(100);
        } catch(Exception e) {
            Progress.SetErrorStatus(-1);
            throw e;
        }
    }

    int AddInstrumentsJob::GetFileCount() {
        int count = 0;

        DIR* pDir = opendir(FsDir.c_str());
        if (pDir == NULL) {
            std::stringstream ss;
            ss << "The scanning of directory `" << FsDir << "` failed: ";
            ss << strerror(errno);
            std::cerr << ss.str();
            return 0;
        }

        struct dirent* pEnt = readdir(pDir);
        while (pEnt != NULL) {
            if (pEnt->d_type != DT_REG) {
                pEnt = readdir(pDir);
                continue;
            }

            String s(pEnt->d_name);
            if(s.length() < 4) {
                pEnt = readdir(pDir);
                continue;
            }
            if(!strcasecmp(".gig", s.substr(s.length() - 4).c_str())) count++;

            pEnt = readdir(pDir);
        }
        
        if (closedir(pDir)) {
            std::stringstream ss;
            ss << "Failed to close directory `" << FsDir << "`: ";
            ss << strerror(errno);
            std::cerr << ss.str();
        }
        
        return count;
    }

    AddInstrumentsFromFileJob::AddInstrumentsFromFileJob(int JobId, String DbDir, String FilePath, int Index) {
        this->JobId = JobId;
        Progress.JobId = JobId;
        Progress.SetTotalFileCount(1);

        this->DbDir = DbDir;
        this->FilePath = FilePath;
        this->Index = Index;
    }

    void AddInstrumentsFromFileJob::Run() {
        try {
            InstrumentsDb::GetInstrumentsDb()->AddInstruments(DbDir, FilePath, Index, &Progress);

            // Just to be sure that the frontends will be notified about the job completion
            if (Progress.GetTotalFileCount() != Progress.GetScannedFileCount()) {
                Progress.SetTotalFileCount(Progress.GetScannedFileCount());
            }
            if (Progress.GetStatus() != 100) Progress.SetStatus(100);
        } catch(Exception e) {
            Progress.SetErrorStatus(-1);
            throw e;
        }
    }


    String DirectoryScanner::DbDir;
    String DirectoryScanner::FsDir;
    bool DirectoryScanner::Flat;
    ScanProgress* DirectoryScanner::pProgress;

    void DirectoryScanner::Scan(String DbDir, String FsDir, bool Flat, ScanProgress* pProgress) {
        dmsg(2,("DirectoryScanner: Scan(DbDir=%s,FsDir=%s,Flat=%d)\n", DbDir.c_str(), FsDir.c_str(), Flat));
        if (DbDir.empty() || FsDir.empty()) throw Exception("Directory expected");
        
        struct stat statBuf;
        int res = stat(FsDir.c_str(), &statBuf);
        if (res) {
            std::stringstream ss;
            ss << "Fail to stat `" << FsDir << "`: " << strerror(errno);
            throw Exception(ss.str());
        }

        if (!S_ISDIR(statBuf.st_mode)) {
            throw Exception("Directory expected");
        }
        
        DirectoryScanner::DbDir = DbDir;
        DirectoryScanner::FsDir = FsDir;
        if (DbDir.at(DbDir.length() - 1) != '/') {
            DirectoryScanner::DbDir.append("/");
        }
        if (FsDir.at(FsDir.length() - 1) != '/') {
            DirectoryScanner::FsDir.append("/");
        }
        DirectoryScanner::Flat = Flat;
        DirectoryScanner::pProgress = pProgress;
        
        ftw(FsDir.c_str(), FtwCallback, 10);
    }

    int DirectoryScanner::FtwCallback(const char* fpath, const struct stat* sb, int typeflag) {
        dmsg(2,("DirectoryScanner: FtwCallback(fpath=%s)\n", fpath));
        if (typeflag != FTW_D) return 0;

        String dir = DbDir;
        if (!Flat) {
            String subdir = fpath;
            if(subdir.length() > FsDir.length()) {
                subdir = subdir.substr(FsDir.length());
                dir += subdir;
            }
        }
        
        InstrumentsDb* db = InstrumentsDb::GetInstrumentsDb();

        if (HasInstrumentFiles(String(fpath))) {
            if (!db->DirectoryExist(dir)) db->AddDirectory(dir);
            db->AddInstrumentsNonrecursive(dir, String(fpath), pProgress);
        }

        return 0;
    };

    bool DirectoryScanner::HasInstrumentFiles(String Dir) {
        return InstrumentFileCounter::Count(Dir) > 0;
    }

    int InstrumentFileCounter::FileCount;

    int InstrumentFileCounter::Count(String FsDir) {
        dmsg(2,("InstrumentFileCounter: Count(FsDir=%s)\n", FsDir.c_str()));
        if (FsDir.empty()) throw Exception("Directory expected");
        FileCount = 0;

        struct stat statBuf;
        int res = stat(FsDir.c_str(), &statBuf);
        if (res) {
            std::stringstream ss;
            ss << "Fail to stat `" << FsDir << "`: " << strerror(errno);
            throw Exception(ss.str());
        }

        if (!S_ISDIR(statBuf.st_mode)) {
            throw Exception("Directory expected");
        }
        
        ftw(FsDir.c_str(), FtwCallback, 10);
        return FileCount;
    }

    int InstrumentFileCounter::FtwCallback(const char* fpath, const struct stat* sb, int typeflag) {
        if (typeflag != FTW_F) return 0;
        String s = fpath;
        if(s.length() < 4) return 0;
        if(!strcasecmp(".gig", s.substr(s.length() - 4).c_str())) FileCount++;

        return 0;
    };

} // namespace LinuxSampler

#endif // HAVE_SQLITE3