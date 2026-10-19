namespace ProjectProject01BuildServerHelper;

/** An OS file-share lock, shared by packaging and publishing across helper instances. */
internal static class ProjectOperationLock
{
    internal static FileStream Acquire(string projectRoot)
    {
        string saved=Path.Combine(Path.GetFullPath(projectRoot),"Saved");
        Directory.CreateDirectory(saved);
        return new FileStream(Path.Combine(saved,"BuildPublish.operation.lock"),FileMode.OpenOrCreate,FileAccess.ReadWrite,FileShare.None);
    }
}
