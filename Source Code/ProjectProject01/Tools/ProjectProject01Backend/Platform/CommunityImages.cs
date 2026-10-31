using System.Buffers.Binary;
using MySqlConnector;

internal static partial class CommunityEndpoints
{
    internal const int MaxImageBytes=5*1024*1024;
    internal static bool IsSafeImage(ReadOnlySpan<byte> data,out string contentType)
    {
        contentType="";
        bool Dimensions(int width,int height)=>width>0&&height>0&&width<=8192&&height<=8192&&(long)width*height<=16_000_000;
        if(data.Length>=33&&data[..8].SequenceEqual(new byte[]{137,80,78,71,13,10,26,10})&&data.Slice(12,4).SequenceEqual("IHDR"u8))
        {
            if(!Dimensions(BinaryPrimitives.ReadInt32BigEndian(data.Slice(16,4)),BinaryPrimitives.ReadInt32BigEndian(data.Slice(20,4))))return false;
            contentType="image/png";return true;
        }
        if(data.Length<4||data[0]!=0xff||data[1]!=0xd8||data[^2]!=0xff||data[^1]!=0xd9)return false;
        int index=2;
        while(index+4<=data.Length)
        {
            if(data[index++]!=0xff)return false;
            while(index<data.Length&&data[index]==0xff)index++;
            if(index>=data.Length)return false;
            byte marker=data[index++];
            if(marker is 0xda or 0xd9)return false;
            if(marker is 0x01 or >=0xd0 and <=0xd7)continue;
            if(index+2>data.Length)return false;
            int length=BinaryPrimitives.ReadUInt16BigEndian(data.Slice(index,2));
            if(length<2||index+length>data.Length)return false;
            if(marker is 0xc0 or 0xc1 or 0xc2)
            {
                if(length<8||!Dimensions(BinaryPrimitives.ReadUInt16BigEndian(data.Slice(index+5,2)),BinaryPrimitives.ReadUInt16BigEndian(data.Slice(index+3,2))))return false;
                contentType="image/jpeg";return true;
            }
            index+=length;
        }
        return false;
    }
    private static void MapCommunityImages(this WebApplication app)
    {
        app.MapPost("/api/admin/community/images",async(HttpRequest req,AdministratorOptions admin,AuthDatabase db,CancellationToken ct)=>
        {
            if(!admin.IsAuthorized(req.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();
            if(req.ContentLength>MaxImageBytes)return Results.StatusCode(413);
            using var memory=new MemoryStream();byte[] buffer=new byte[16384];int count;
            while((count=await req.Body.ReadAsync(buffer,ct))>0)
            {if(memory.Length+count>MaxImageBytes)return Results.StatusCode(413);await memory.WriteAsync(buffer.AsMemory(0,count),ct);}
            byte[] data=memory.ToArray();
            if(!IsSafeImage(data,out string type))return Error("PNG·JPG만 첨부할 수 있습니다. 최대 5MB, 8192px 이내, 1600만 화소 이내입니다.");
            await using var c=await db.OpenConnectionAsync(ct);
            var capacity=await Rows(c,null,"SELECT COALESCE(SUM(OCTET_LENGTH(data)),0) size FROM launcher_images",ct);
            if(Convert.ToInt64(capacity[0]["size"])+data.Length>256L*1024*1024)return Error("공지 이미지 보관 한도(256MB)에 도달했습니다.");
            string id=Guid.NewGuid().ToString("D");
            await Exec(c,null,"INSERT INTO launcher_images(id,content_type,data) VALUES(@id,@type,@data)",ct,("id",id),("type",type),("data",data));
            return Results.Ok(new {id,markup=$"![사진](image:{id})",message="본문에 이미지가 첨부되었습니다."});
        }).RequireRateLimiting("admin").WithMetadata(new Microsoft.AspNetCore.Mvc.RequestSizeLimitAttribute(MaxImageBytes));
        app.MapGet("/api/launcher/images/{id:guid}",async(Guid id,HttpResponse response,AuthDatabase db,CancellationToken ct)=>
        {
            await using var c=await db.OpenConnectionAsync(ct);
            var rows=await Rows(c,null,"""
                SELECT i.data,i.content_type FROM launcher_images i WHERE i.id=@id
                AND EXISTS(SELECT 1 FROM launcher_posts p WHERE p.published=1 AND INSTR(LOWER(p.body),CONCAT('(image:',@id,')'))>0)
                """,ct,("id",id.ToString("D")));
            if(rows.Count==0)return Results.NotFound();
            response.Headers["X-Content-Type-Options"]="nosniff";
            return Results.File((byte[])rows[0]["data"]!,(string)rows[0]["content_type"]!);
        }).RequireRateLimiting("survey-read");
        app.MapGet("/api/admin/community/images/{id:guid}",async(Guid id,HttpRequest req,HttpResponse response,AdministratorOptions admin,AuthDatabase db,CancellationToken ct)=>
        {
            if(!admin.IsAuthorized(req.Headers["X-ProjectProject01-Admin-Key"]))return Results.Unauthorized();
            await using var c=await db.OpenConnectionAsync(ct);
            var rows=await Rows(c,null,"SELECT data,content_type FROM launcher_images WHERE id=@id",ct,("id",id.ToString("D")));
            if(rows.Count==0)return Results.NotFound();response.Headers["X-Content-Type-Options"]="nosniff";
            return Results.File((byte[])rows[0]["data"]!,(string)rows[0]["content_type"]!);
        }).RequireRateLimiting("admin");
    }
}
