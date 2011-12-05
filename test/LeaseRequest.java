import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.IOException;
import java.net.InetAddress;
import java.net.Socket;
import java.net.UnknownHostException;


public class LeaseRequest {
	public static void main(String[] args) throws UnknownHostException, IOException {
		Socket s = new Socket(InetAddress.getLocalHost().getHostAddress(), 26001);
		
		BufferedInputStream in = new BufferedInputStream(s.getInputStream());
		BufferedOutputStream out = new BufferedOutputStream(s.getOutputStream());
		
		byte[] buff = new byte[137];
		int r;
		
		System.out.println("Starting request sequence to " + InetAddress.getLocalHost() + ":26001 ...");
		
		out.write(9); // Send junk
		out.flush();
		
		r = in.read(buff); // Receive env
		System.out.println("Return request type: " + buff[0] + ", size: " + r + ", value: " + new String(buff));
		
		// Set up connection again!
		s = new Socket(InetAddress.getLocalHost().getHostAddress(), 26001);
		
		in = new BufferedInputStream(s.getInputStream());
		out = new BufferedOutputStream(s.getOutputStream());
		
		out.write(buff); // Send env
		out.flush();
		
		r = in.read(buff);
	}
}
